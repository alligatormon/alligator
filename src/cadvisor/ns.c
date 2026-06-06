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

#endif