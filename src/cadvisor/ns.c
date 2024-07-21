#ifdef __linux__
#define _GNU_SOURCE
#include <stdio.h>
#include <linux/sched.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sched.h>
#include "cadvisor/ns.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

int net_ns_mount(int fd, char *name)
{
	int rc = setns(fd, CLONE_NEWNET);
	carglog(ac->cadvisor_carg, L_INFO, "setns return: %d\n", rc);

	if (rc < 0)
		return rc;

	mount(name, "/var/lib/alligator/nsmount/", "sysfs", 0, NULL);

	return 1;
}


int mount_ns_by_cgroup_procs(char *dirpath, char *name)
{
	FILE *fd;
	char buf[1000];
	fd = fopen(dirpath, "r");
	if (!fd)
		return 0;

	if (!fgets(buf, 1000, fd))
	{
		fclose(fd);
		return 0;
	}
	buf[strlen(buf)-1] = 0;
	fclose(fd);

	snprintf(dirpath, 1000, "/proc/%s/ns/net", buf);
	int sfd = open(dirpath, O_RDONLY);
	if (sfd < 0)
		return 0;

	int rc = net_ns_mount(sfd, name);
	if (rc < 0)
		return 0;

	close(sfd);

	return 1;
}

int mount_ns_by_pid(char *pid)
{
	char dirpath[1000];
	snprintf(dirpath, 1000, "/proc/%s/ns/net", pid);
	int sfd = open(dirpath, O_RDONLY);
	if (sfd < 0)
		return 0;

	int rc = setns(sfd, CLONE_NEWNET);
	carglog(ac->cadvisor_carg, L_INFO, "setns return: %d\n", rc);

	close(sfd);

	return rc;
}

//char* get_ifname_by_cgroup_id(char *slice, char *cntid, alligator_ht *ifhash, char *name)
//{
//	FILE *fd;
//	char dirpath[1000];
//	char buf[1000];
//	snprintf(dirpath, 1000, "/sys/fs/cgroup/pids/%s/%s/cgroup.procs", slice, cntid);
//	fd = fopen(dirpath, "r");
//	if (!fd)
//		return NULL;
//
//	fgets(buf, 1000, fd);
//	buf[strlen(buf)-1] = 0;
//	fclose(fd);
//
//	snprintf(dirpath, 1000, "/proc/%s/ns/net", buf);
//	int sfd = open(dirpath, O_RDONLY);
//	net_ns_mount(sfd, name);
//	fd = fopen("/var/lib/alligator/nsmount/class/net/eth0/iflink", "r");
//	if (!fd)
//		return NULL;
//
//	fgets(buf, 1000, fd);
//	buf[strlen(buf)-1] = 0;
//	fclose(fd);
//	umount("/var/lib/alligator/nsmount");
//	unshare(CLONE_NEWNET);
//
//	uint32_t id_ifhash = tommy_strhash_u32(0, buf);
//	ifindexnames *iindex = alligator_ht_search(ifhash, ifindexnames_compare, buf, id_ifhash);
//	if (!iindex)
//		return NULL;
//
//
//	return iindex->value;
//}
#endif
