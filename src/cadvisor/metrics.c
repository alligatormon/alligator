#ifdef __linux__
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <mntent.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <linux/sched.h>
#include "main.h"
#include "cadvisor/metrics.h"
#include "cadvisor/ns.h"
#include <sys/mount.h>
#include "common/logs.h"
#define PATH_SIZE 1000
int unshare(int flags);

extern aconf *ac;

typedef struct disk_list_id
{
	char id[41];
	char devname[1000];

	tommy_node node;
} disk_list_id;

typedef struct mnt_list
{
	char mountpoint[1000];
	char mountname[1000];
	uint64_t total;
	uint64_t avail;
	uint64_t used;
	uint64_t inodes_total;
	uint64_t inodes_avail;
	uint64_t inodes_used;

	tommy_node node;
} mnt_list;

int get_int_file(char *fpath, int64_t *ret)
{
	char buf[1000];

	FILE *fd = fopen(fpath, "r");
	if (!fd)
		return 0;

	if (fgets(buf, 1000, fd))
		*ret = strtoll(buf, NULL, 10);

	fclose(fd);

	return 1;
}

void mlist_foreach_free(void *funcarg, void* arg)
{
	mnt_list *mlist = arg;
	if (!mlist)
		return;

	//free(mlist->mountpoint);
	//free(mlist->mountname);

	free(mlist);
}

void mlist_free(alligator_ht* mlist_hash)
{
	alligator_ht_foreach_arg(mlist_hash, mlist_foreach_free, NULL);
	alligator_ht_done(mlist_hash);
	free(mlist_hash);
}

void dlid_foreach_free(void *funcarg, void* arg)
{
	disk_list_id *dlid = arg;
	if (!dlid)
		return;

	free(dlid);
}

void dlid_free(alligator_ht* dlid_hash)
{
	alligator_ht_foreach_arg(dlid_hash, dlid_foreach_free, NULL);
	alligator_ht_done(dlid_hash);
	free(dlid_hash);
}

void add_cadvisor_metric_uint(char *mname, uint64_t val, char *cntid, char *name, char *image, char *cad_id, char *name1, char *value1, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	alligator_ht *hash = alligator_ht_init(NULL);
	labels_hash_insert(hash, "name", name);
	if (image)
		labels_hash_insert(hash, "image", image);
	if (kubenamespace)
		labels_hash_insert(hash, "namespace", kubenamespace);
	if (kubepod)
		labels_hash_insert(hash, "pod_name", kubepod);
	if (kubecontainer)
		labels_hash_insert(hash, "container_name", kubecontainer);
	if (libvirt_id)
		labels_hash_insert(hash, "libvirt_id", libvirt_id);
	if (name1 && value1)
		labels_hash_insert(hash, name1, value1);
	labels_hash_insert(hash, "id", cad_id);

	carglog(ac->cadvisor_carg, L_INFO, "%s:%s:%s:%s:%s:%s:%s:%s:%s %"PRIu64"\n", mname ? mname : "", cad_id ? cad_id : "", cntid ? cntid : "", name1 ? name1 : "", value1 ? value1 : "", kubenamespace ? kubenamespace : "", kubepod ? kubepod : "", kubecontainer ? kubecontainer : "", val, libvirt_id ? libvirt_id : "");

	metric_add(mname, hash, &val, DATATYPE_UINT, ac->cadvisor_carg);
}

void add_cadvisor_metric_int(char *mname, int64_t val, char *cntid, char *name, char *image, char *cad_id, char *name1, char *value1, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	alligator_ht *hash = alligator_ht_init(NULL);
	labels_hash_insert(hash, "name", name);
	if (image)
		labels_hash_insert(hash, "image", image);
	if (kubenamespace)
		labels_hash_insert(hash, "namespace", kubenamespace);
	if (kubepod)
		labels_hash_insert(hash, "pod_name", kubepod);
	if (kubecontainer)
		labels_hash_insert(hash, "container_name", kubecontainer);
	if (libvirt_id)
		labels_hash_insert(hash, "libvirt_id", libvirt_id);
	if (name1 && value1)
		labels_hash_insert(hash, name1, value1);
	labels_hash_insert(hash, "id", cad_id);

	carglog(ac->cadvisor_carg, L_INFO, "%s:%s:%s:%s:%s %"PRId64"\n", mname, cad_id, cntid, name1, value1, val);

	metric_add(mname, hash, &val, DATATYPE_INT, ac->cadvisor_carg);
}

void add_cadvisor_metric_double(char *mname, double val, char *cntid, char *name, char *image, char *cad_id, char *name1, char *value1, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	alligator_ht *hash = alligator_ht_init(NULL);
	labels_hash_insert(hash, "name", name);
	if (image)
		labels_hash_insert(hash, "image", image);
	if (kubenamespace)
		labels_hash_insert(hash, "namespace", kubenamespace);
	if (kubepod)
		labels_hash_insert(hash, "pod_name", kubepod);
	if (kubecontainer)
		labels_hash_insert(hash, "container_name", kubecontainer);
	if (libvirt_id)
		labels_hash_insert(hash, "libvirt_id", libvirt_id);
	if (name1 && value1)
		labels_hash_insert(hash, name1, value1);
	labels_hash_insert(hash, "id", cad_id);

	carglog(ac->cadvisor_carg, L_INFO, "%s:%s:%s:%s:%s %lf\n", mname, cad_id, cntid, name1, value1, val);

	metric_add(mname, hash, &val, DATATYPE_DOUBLE, ac->cadvisor_carg);
}

void cgroup_get_netinfo(char *sysfs, char *ifname, char *stat, char *mname, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	FILE *fd;
	char buf[PATH_SIZE];
	char readbuf[PATH_SIZE];

	snprintf(buf, PATH_SIZE, "%s/class/net/%s/statistics/%s", sysfs, ifname, stat);
	fd = fopen(buf, "r");
	if (!fd)
		return;

	if (!fgets(readbuf, PATH_SIZE, fd))
	{
		fclose(fd);
		return;
	}

	uint64_t val = strtoull(readbuf, NULL, 10);
	add_cadvisor_metric_uint(mname, val, cntid, name, image, cad_id, "interface", ifname, kubenamespace, kubepod, kubecontainer, libvirt_id);
	// additional by k8s:
	// io.kubernetes.container.name -> container_name
	// io.kubernetes.pod.name -> pod_name
	// io.kubernetes.pod.namespace -> namespace
	fclose(fd);
}

int mlist_hash_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((mnt_list*)obj)->mountpoint;
	return strcmp(s1, s2);
}

alligator_ht* get_disk_ids_names(char *cntid, char *name, char *image, char *cad_id, alligator_ht* mlist_hash, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	char buf[PATH_SIZE];
	char mountpoint[1000];
	char devname[1000];
	size_t devsize;
	char *tmp;
	uint64_t disk_busy;
	strlcpy(devname, "/dev/", 6);

	FILE *fd;
	char diskstatspath[256];
	snprintf(diskstatspath, 255, "%s/diskstats", ac->system_procfs);
	fd = fopen(diskstatspath, "r");
	if (!fd)
		return NULL;

	uint64_t maj, min;
	alligator_ht* dlid_hash = alligator_ht_init(NULL);

	while (fgets(buf, PATH_SIZE, fd))
	{
		disk_list_id *dlid = malloc(sizeof(*dlid));
		maj = strtoull(buf, &tmp, 10);
		tmp += strspn(tmp, " \t");

		min = strtoull(tmp, &tmp, 10);
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");

		char *buf = strstr(tmp, "dm");
		if (!buf)
			buf = tmp;

		mnt_list *mlist = NULL;
		if (mlist_hash)
		{
			strlcpy(mountpoint, buf, strcspn(buf, " ")+1);
			uint32_t id_hash = tommy_strhash_u32(0, mountpoint);
			mlist = alligator_ht_search(mlist_hash, mlist_hash_compare, mountpoint, id_hash);
		}

		if (mlist)
		{
			strlcpy(dlid->devname, mlist->mountname, 1000);
		}
		else
		{
			devsize = strcspn(tmp, " \t");
			strlcpy(devname+5, tmp, devsize+1);
			strlcpy(dlid->devname, devname, 1000);
		}

		snprintf(dlid->id, 41, "%"PRIu64":%"PRIu64, maj, min);
		uint32_t id_hash = tommy_strhash_u32(0, dlid->id);
		alligator_ht_insert(dlid_hash, &(dlid->node), dlid, id_hash);


		uint8_t j = 10;
		while (j--)
		{
			tmp += strcspn(tmp, " \t");
			tmp += strspn(tmp, " \t");
		}
		disk_busy = strtoull(tmp, NULL, 10);

		if (disk_busy)
			add_cadvisor_metric_uint("container_fs_io_time_weighted_seconds_total", disk_busy, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);
	}

	fclose(fd);
	return dlid_hash;
}

int dlid_hash_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((disk_list_id*)obj)->id;
	return strcmp(s1, s2);
}

void cgroup_get_diskinfo(alligator_ht* dlid_hash, char *prefix, char *cntid, char *stat, char *mname_template, char *name, char *image, char *cad_id, char *wrname, char *rdname, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	FILE *fd;
	char buf[PATH_SIZE];
	char readbuf[PATH_SIZE];
	char disk_id[41];
	char *tmp;
	//char action_type[50];
	char mname[255];

	snprintf(buf, PATH_SIZE, "%s/fs/cgroup/blkio/%s/%s/blkio.%s", ac->system_sysfs, prefix, cntid, stat);

	// check what the blkio.stat is counting, otherwise take blkio.throttle.stat
	fd = fopen(buf, "r");
	if (!fd)
		snprintf(buf, PATH_SIZE, "%s/fs/cgroup/blkio/%s/%s/blkio.throttle.%s", ac->system_sysfs, prefix, cntid, stat);
	else
	{
		if(fgets(readbuf, PATH_SIZE, fd))
		{
			if (!strncmp(readbuf, "Total 0", 7))
				snprintf(buf, PATH_SIZE, "%s/fs/cgroup/blkio/%s/%s/blkio.throttle.%s", ac->system_sysfs, prefix, cntid, stat);
		}
		fclose(fd);
	}
	// end

	fd = fopen(buf, "r");
	if (!fd)
		return;

	while(fgets(readbuf, PATH_SIZE, fd))
	{
		size_t read_size = strcspn(readbuf, " \t");
		strlcpy(disk_id, readbuf, read_size+1);

		tmp = readbuf + read_size;
		tmp += strspn(tmp, " \t");
		if (!strncmp(tmp, "Write", 5))
			snprintf(mname, 255, mname_template, wrname);
		else if (!strncmp(tmp, "Read", 4))
			snprintf(mname, 255, mname_template, rdname);
		else
			continue;

		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");

		uint64_t val = strtoull(tmp, NULL, 10);
		if (!val)
			continue; // take away all disks, only used

		uint32_t id_hash = tommy_strhash_u32(0, disk_id);
		disk_list_id *dlid = alligator_ht_search(dlid_hash, dlid_hash_compare, disk_id, id_hash);
		if (dlid)
		{
			//printf("%s:%s:%s:%s %"PRIu64"\n", mname, cntid, dlid->devname, stat, val);
			add_cadvisor_metric_uint(mname, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, NULL);
		}
	}
	fclose(fd);
}

void cgroup2_get_diskinfo(context_arg *carg, alligator_ht* dlid_hash, char *prefix, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id, char *file, char *rbps, char *wbps, char *riops, char *wiops)
{
	FILE *fd;
	char buf[PATH_SIZE];
	char readbuf[PATH_SIZE];
	char disk_id[41];
	char *tmp;

	snprintf(buf, PATH_SIZE, "%s/fs/cgroup/%s/%s/%s", ac->system_sysfs, prefix, cntid, file);

	fd = fopen(buf, "r");
	carglog(carg, L_INFO, "cgroup2_get_diskinfo tries open: '%s': %d\n", buf, fd);
	if (!fd)
		return;

	while(fgets(readbuf, PATH_SIZE, fd))
	{
		size_t read_size = strcspn(readbuf, " \t");
		strlcpy(disk_id, readbuf, read_size+1);

		tmp = readbuf + read_size;
		tmp += strspn(tmp, " \t");
		uint32_t id_hash = tommy_strhash_u32(0, disk_id);
		disk_list_id *dlid = alligator_ht_search(dlid_hash, dlid_hash_compare, disk_id, id_hash);
		if (!dlid)
			continue;

		while (1) {
		    char *type = tmp;
			tmp += strcspn(tmp, "=");
			tmp += strspn(tmp, "=");

			uint64_t val = strtoull(tmp, NULL, 10);

			if (strncmp(tmp, "max", 3) && val) {
				if (!strncmp(type, "rbytes", 6))
					add_cadvisor_metric_uint(rbps, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);

				else if (!strncmp(type, "rbps", 4))
					add_cadvisor_metric_uint(rbps, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);

				else if (!strncmp(type, "wbytes", 6))
					add_cadvisor_metric_uint(wbps, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);

				else if (!strncmp(type, "wbps", 4))
					add_cadvisor_metric_uint(wbps, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);

				else if (!strncmp(type, "rios", 4))
					add_cadvisor_metric_uint(riops, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);

				else if (!strncmp(type, "riops", 5))
					add_cadvisor_metric_uint(riops, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);

				else if (!strncmp(type, "wios", 4))
					add_cadvisor_metric_uint(wiops, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);

				else if (!strncmp(type, "wiops", 4))
					add_cadvisor_metric_uint(wiops, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);
			}

			tmp += strcspn(tmp, " \t");
			tmp += strspn(tmp, " \t");
			if (!*tmp)
				break;
		}

	}
	fclose(fd);
}

void cgroup_get_diskinfo_total(alligator_ht* dlid_hash, char *prefix, char *cntid, char *stat, char *mname, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	FILE *fd;
	char buf[PATH_SIZE];
	char readbuf[PATH_SIZE];
	char disk_id[41];
	char *tmp;
	//char action_type[50];

	snprintf(buf, PATH_SIZE, "%s/fs/cgroup/blkio/%s/%s/blkio.%s", ac->system_sysfs, prefix, cntid, stat);
	fd = fopen(buf, "r");
	if (!fd)
		return;

	while(fgets(readbuf, PATH_SIZE, fd))
	{
		size_t read_size = strcspn(readbuf, " \t");
		strlcpy(disk_id, readbuf, read_size+1);

		tmp = readbuf + read_size;
		tmp += strspn(tmp, " \t");
		if (strncmp(tmp, "Total", 5))
			continue;

		read_size = strcspn(tmp, " \t");

		uint64_t val = strtoull(readbuf+read_size+1, NULL, 10)/1000000000;

		uint32_t id_hash = tommy_strhash_u32(0, disk_id);
		disk_list_id *dlid = alligator_ht_search(dlid_hash, dlid_hash_compare, disk_id, id_hash);
		if (dlid)
			add_cadvisor_metric_uint(mname, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, NULL);
	}
	fclose(fd);
}

uint64_t cgroup_get_pid(char *prefix, char *container)
{
	uint64_t pid;
	char fpath[1000];
	char buf[1000];
	snprintf(fpath, 1000, "%s/fs/cgroup/pids/%s/%s/cgroup.procs", ac->system_sysfs, prefix, container);
	FILE *fd = fopen(fpath, "r");
	if (!fd)
	{
		snprintf(fpath, 1000, "%s/fs/cgroup/cpu,cpuacct/%s/%s/cgroup.procs", ac->system_sysfs, prefix, container);
		fd = fopen(fpath, "r");
		if (!fd)
			return 0;
	}

	if (!fgets(buf, 1000, fd))
	{
		fclose(fd);
		return 0;
	}
	fclose(fd);

	pid = strtoull(buf, NULL, 10);
	return pid;
}

void cgroup_fd_disk_part_info(char *prefix, uint64_t pid, char *container, alligator_ht* mlist_hash, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	if (!pid)
		return;

	char fpath[1000];
	char mountpoint[1000];
	struct mntent *fs;
	snprintf(fpath, 1000, "%s/%"PRIu64"/mounts", ac->system_procfs, pid);

	FILE *fp = setmntent(fpath, "r");
	if (fp == NULL) {
		fprintf(stderr, "could not open %s\n ", fpath);
		return;
	}

	while ((fs = getmntent(fp)) != NULL)
	{
		//printf("mnt type '%s' mnt_dir '%s', mnt_opts '%s', mnt_fsname '%s'\n", fs->mnt_type, fs->mnt_dir, fs->mnt_opts, fs->mnt_fsname);

		struct statvfs vfsbuf;
		if (statvfs(fs->mnt_dir, &vfsbuf) == -1)
		{
			//perror("statvfs() error");
			continue;
		}

		char *buf = strstr(fs->mnt_fsname, "dm");
		if (!buf)
		{
			if (!strcmp(fs->mnt_type,"xfs") || !strcmp(fs->mnt_type,"ext4") || !strcmp(fs->mnt_type,"btrfs") || !strcmp(fs->mnt_type,"ext3") || !strcmp(fs->mnt_type,"ext2"))
				buf = fs->mnt_fsname;
			else if (!strcmp(fs->mnt_type, "overlay"))
				buf = fs->mnt_opts;
			else
				continue;
		}

		strlcpy(mountpoint, buf, 1000);
		uint32_t id_hash = tommy_strhash_u32(0, mountpoint);
		mnt_list *mlist = alligator_ht_search(mlist_hash, mlist_hash_compare, mountpoint, id_hash);
		if (mlist)
		{
			//printf("%s:%s %"PRIu64"/%"PRIu64"/%"PRIu64"/%"PRIu64"/%"PRIu64"/%"PRIu64"""\n", "mname", prefix, mlist->mountpoint, mlist->total, mlist->avail, mlist->used, mlist->inodes_total, mlist->inodes_avail, mlist->inodes_used);
			add_cadvisor_metric_uint("container_fs_inodes_free", mlist->inodes_avail, container, name, image, cad_id, "device", mlist->mountname, kubenamespace, kubepod, kubecontainer, NULL);
			add_cadvisor_metric_uint("container_fs_inodes_usage", mlist->inodes_used, container, name, image, cad_id, "device", mlist->mountname, kubenamespace, kubepod, kubecontainer, NULL);
			add_cadvisor_metric_uint("container_fs_inodes_total", mlist->inodes_total, container, name, image, cad_id, "device", mlist->mountname, kubenamespace, kubepod, kubecontainer, NULL);
			add_cadvisor_metric_uint("container_fs_limit_bytes", mlist->total, container, name, image, cad_id, "device", mlist->mountname, kubenamespace, kubepod, kubecontainer, NULL);
			add_cadvisor_metric_uint("container_fs_usage_bytes", mlist->used, container, name, image, cad_id, "device", mlist->mountname, kubenamespace, kubepod, kubecontainer, NULL);
			add_cadvisor_metric_uint("container_fs_free_bytes", mlist->avail, container, name, image, cad_id, "device", mlist->mountname, kubenamespace, kubepod, kubecontainer, NULL);
		}
	}
	fclose(fp);
}

alligator_ht* get_mountpoint_list()
{
	FILE *fp;
	struct mntent *fs;
	alligator_ht* mlist_hash = calloc(1, sizeof(*mlist_hash));
	alligator_ht_init(mlist_hash);

	fp = setmntent("/etc/mtab", "r");
	if (fp == NULL) {
		fprintf(stderr, "could not open /etc/mtab\n " );
		mlist_free(mlist_hash);
		return NULL;
	}

	while ((fs = getmntent(fp)) != NULL)
	{
		//printf("mnt type '%s' mnt_dir '%s'\n", fs->mnt_type, fs->mnt_dir);

		//int mnt_fd;
		//mnt_fd = open(fs->mnt_dir,O_RDONLY);
		//if(mnt_fd == -1)
		//	continue;

		struct statvfs vfsbuf;
		if (statvfs(fs->mnt_dir, &vfsbuf) == -1)
		{
			//close(mnt_fd);
			//perror("statvfs() error");
			continue;
		}

		mnt_list *mlist = malloc(sizeof(*mlist));
		strlcpy(mlist->mountpoint, fs->mnt_dir, 1000);
		strlcpy(mlist->mountname, fs->mnt_fsname, 1000);
		mlist->total = ((double)vfsbuf.f_blocks * vfsbuf.f_bsize);
		mlist->avail = ((double)vfsbuf.f_bavail * vfsbuf.f_bsize);
		mlist->used = mlist->total - mlist->avail;
		mlist->inodes_total = vfsbuf.f_files;
		mlist->inodes_avail = vfsbuf.f_favail;
		mlist->inodes_used = mlist->inodes_total-mlist->inodes_avail;

		uint32_t id_hash = tommy_strhash_u32(0, mlist->mountpoint);
		alligator_ht_insert(mlist_hash, &(mlist->node), mlist, id_hash);

		char linkurl[255];
		ssize_t len = readlink(fs->mnt_fsname, linkurl, 254);
		if (len > 0)
		{
			linkurl[len] = 0;
			//printf("%s(%s) -> %s\n", fs->mnt_fsname, linkurl, fs->mnt_dir);
			mnt_list *dmlist = malloc(sizeof(*dmlist));
			memcpy(dmlist, mlist, sizeof(*dmlist));
			strlcpy(dmlist->mountpoint, linkurl+3, 1000);
			strlcpy(dmlist->mountname, fs->mnt_fsname, 1000);
	
			id_hash = tommy_strhash_u32(0, dmlist->mountpoint);
			alligator_ht_insert(mlist_hash, &(dmlist->node), dmlist, id_hash);
		}

		mnt_list *nmlist = malloc(sizeof(*nmlist));
		memcpy(nmlist, mlist, sizeof(*nmlist));
		strlcpy(nmlist->mountpoint, fs->mnt_fsname, 1000);
		strlcpy(nmlist->mountname, fs->mnt_dir, 1000);
	
		id_hash = tommy_strhash_u32(0, nmlist->mountpoint);
		alligator_ht_insert(mlist_hash, &(nmlist->node), nmlist, id_hash);

		mnt_list *olist = malloc(sizeof(*olist));
		memcpy(olist, mlist, sizeof(*olist));
		strlcpy(olist->mountpoint, fs->mnt_opts, 1000);
		strlcpy(olist->mountname, fs->mnt_dir, 1000);
	
		id_hash = tommy_strhash_u32(0, olist->mountpoint);
		alligator_ht_insert(mlist_hash, &(olist->node), olist, id_hash);
	}

	endmntent(fp);
	return mlist_hash;
}

void cgroup_get_cpuinfo(char *prefix, char *cntid, char *stat, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char fpath[1000];
	char buf[1000];
	char *tmp;
	//size_t read_size;
	uint64_t val;
	uint64_t system = 0, user = 0;
	snprintf(fpath, 1000, "%s/fs/cgroup/cpu,cpuacct/%s/%s/%s", ac->system_sysfs, prefix, cntid, stat);
	FILE *fd = fopen(fpath, "r");
	if (!fd)
		return;

	while(fgets(buf, 1000, fd))
	{
		tmp = buf;
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		val = strtoull(tmp, NULL, 10);
		if (!strncmp(buf, "nr_periods", 10))
			add_cadvisor_metric_uint("container_cpu_cfs_periods_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "nr_throttled", 12))
			add_cadvisor_metric_uint("container_cpu_cfs_throttled_periods_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "throttled_time", 14))
			add_cadvisor_metric_uint("container_cpu_cfs_throttled_seconds_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "system", 6))
		{
			system = val;
			add_cadvisor_metric_uint("container_cpu_system_seconds_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
		else if (!strncmp(buf, "user", 4))
		{
			user = val;
			add_cadvisor_metric_uint("container_cpu_user_seconds_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
	}
	fclose(fd);

	if (system || user)
	{
		val = system + user;
		add_cadvisor_metric_uint("container_cpu_all_seconds_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	}
}

void cgroup_get_cpuacctinfo(char *prefix, char *cntid, char *stat, char *mname, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char fpath[1000];
	char buf[1000];
	char *tmp;
	char cpuname[10];
	size_t read_size;
	uint64_t val;
	snprintf(fpath, 1000, "%s/fs/cgroup/cpu,cpuacct/%s/%s/%s", ac->system_sysfs, prefix, cntid, stat);
	FILE *fd = fopen(fpath, "r");
	if (!fd)
		return;

	if(!fgets(buf, 1000, fd))
	{
		fclose(fd);
		return;
	}
	tmp = buf;
	read_size = strlen(tmp);

	uint64_t i;
	for (i=0; tmp-buf<read_size; ++i)
	{
		val = strtoull(tmp, NULL, 10);
		if (val == 0)
			break;

		double fval = (double)val/1000000000.0;
		snprintf(cpuname, 10, "cpu%02"PRIu64, i);
		//printf("[%3zu/%zu] %s:%s:%s:%s:%s %lf\n", tmp-buf, read_size, mname, stat, cpuname, prefix, cntid, fval);
		add_cadvisor_metric_double(mname, fval, cntid, name, image, cad_id, "cpu", cpuname, kubenamespace, kubepod, kubecontainer, NULL);
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
	}
		//printf("container_cpu_cfs_throttled_seconds_total:%s:%s %"PRIu64"\n", prefix, cntid, val);

	fclose(fd);
}

uint64_t cadvisor_files_num(char *path)
{
	struct dirent *entry;
	DIR *dp;

	dp = opendir(path);
	if (!dp)
	{
		return 0;
	}

	uint64_t i = 0;
	while((entry = readdir(dp)))
	{
		if ( entry->d_name[0] == '.' )
			continue;
		++i;
	}

	closedir(dp);

	return i;
}

void cgroup_cpu_schedstats_info(char *prefix, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char fpath[1000];
	char buf[1000];
	//char mountpoint[1000];
	char *tmp;
	uint64_t pid;
	//size_t read_size;
	uint64_t run_periods = 0;
	uint64_t runqueue_time = 0;
	uint64_t run_time = 0;

	uint64_t running = 0;
	uint64_t sleeping = 0;
	uint64_t uninterruptible = 0;
	uint64_t zombie = 0;
	uint64_t stopped = 0;
	uint64_t open_files = 0;

	snprintf(fpath, 1000, "%s/fs/cgroup/pids/%s/%s/cgroup.procs", ac->system_sysfs, prefix, cntid);
	FILE *fd = fopen(fpath, "r");
	if (!fd)
	{
		snprintf(fpath, 1000, "%s/fs/cgroup/cpu,cpuacct/%s/%s/cgroup.procs", ac->system_sysfs, prefix, cntid);
		fd = fopen(fpath, "r");
		if (!fd)
			return;
	}

	while (fgets(buf, 1000, fd))
	{
		pid = strtoull(buf, NULL, 10);

		// schedstat
		snprintf(fpath, 1000, "%s/%"PRIu64"/schedstat", ac->system_procfs, pid);

		FILE *sched_fd = fopen(fpath, "r");
		if (!sched_fd)
			continue;

		if (!fgets(buf, 1000, sched_fd))
		{
			perror("fgets:");
			fclose(sched_fd);
			continue;
		}
		fclose(sched_fd);

		tmp = buf;
		run_time += strtoull(tmp, &tmp, 10);

		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		runqueue_time += strtoull(tmp, &tmp, 10);

		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		run_periods += strtoull(tmp, &tmp, 10);

		// running state
		snprintf(fpath, 1000, "%s/%"PRIu64"/stat", ac->system_procfs, pid);

		FILE *state_fd = fopen(fpath, "r");
		if (!state_fd)
			continue;

		if (!fgets(buf, 1000, state_fd))
		{
			perror("fgets:");
			fclose(state_fd);
			continue;
		}
		fclose(state_fd);

		tmp = buf;
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");

		if (*tmp == 'R')
			++running;
		else if (*tmp == 'S')
			++sleeping;
		else if (*tmp == 'D')
			++uninterruptible;
		else if (*tmp == 'Z')
			++zombie;
		else if (*tmp == 'T')
			++stopped;

		snprintf(fpath, 1000, "%s/%"PRIu64"/fd", ac->system_procfs, pid);

		open_files += cadvisor_files_num(fpath);
	}

	//printf("container_cpu_schedstat_run_periods_total:%s:%s %"PRIu64"\n", prefix, cntid, run_periods);
	//printf("container_cpu_schedstat_runqueue_seconds_total:%s:%s %"PRIu64"\n", prefix, cntid, runqueue_time);
	//printf("container_cpu_schedstat_run_seconds_total:%s:%s %"PRIu64"\n", prefix, cntid, run_time);

	//printf("container_tasks_state:%s:%s:state:running %"PRIu64"\n", prefix, cntid, running);
	//printf("container_tasks_state:%s:%s:state:sleeping %"PRIu64"\n", prefix, cntid, sleeping);
	//printf("container_tasks_state:%s:%s:state:uninterruptible %"PRIu64"\n", prefix, cntid, uninterruptible);
	//printf("container_tasks_state:%s:%s:state:zombie %"PRIu64"\n", prefix, cntid, zombie);
	//printf("container_tasks_state:%s:%s:state:stopped %"PRIu64"\n", prefix, cntid, stopped);

	add_cadvisor_metric_uint("container_cpu_schedstat_run_periods_total", run_periods, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_uint("container_cpu_schedstat_runqueue_seconds_total", runqueue_time, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_uint("container_cpu_schedstat_run_seconds_total", run_time, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);

	add_cadvisor_metric_uint("container_tasks_state", running, cntid, name, image, cad_id, "state", "running", kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_uint("container_tasks_state", sleeping, cntid, name, image, cad_id, "state", "sleeping", kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_uint("container_tasks_state", uninterruptible, cntid, name, image, cad_id, "state", "uninterruptible", kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_uint("container_tasks_state", zombie, cntid, name, image, cad_id, "state", "zombie", kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_uint("container_tasks_state", stopped, cntid, name, image, cad_id, "state", "stopped", kubenamespace, kubepod, kubecontainer, NULL);

	add_cadvisor_metric_uint("container_open_files", open_files, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);

	fclose(fd);
}

void cgroup_memory_info(char *prefix, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char fpath[1000];
	char buf[1000];
	char *tmp;
	uint64_t val;
	uint64_t working_set = 0;
	snprintf(fpath, 1000, "%s/fs/cgroup/memory/%s/%s/memory.stat", ac->system_sysfs, prefix, cntid);

	FILE *fd = fopen(fpath, "r");
	carglog(ac->cadvisor_carg, L_INFO, "cadvisor tries open '%s': %p\n", fpath, fd);
	if (!fd)
		return;

	while(fgets(buf, 1000, fd))
	{
		tmp = buf;
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		val = strtoull(tmp, NULL, 10);
		if (!strncmp(buf, "total_swap", 10))
			add_cadvisor_metric_uint("container_memory_swap", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "total_rss ", 10))
		{
			working_set += val;
			add_cadvisor_metric_uint("container_memory_rss", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
			carglog(ac->cadvisor_carg, L_INFO, "container_memory_rss sysfs %s, cntid %s, name %s, image %s, cad_id %s, kubenamespace %s, kubepod %s, kubecontainer %s\n", ac->system_sysfs, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);
		}
		else if (!strncmp(buf, "total_swap ", 11))
		{
			working_set += val;
		}
		else if (!strncmp(buf, "total_cache ", 12))
			add_cadvisor_metric_uint("container_memory_cache", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "total_mapped_file ", 18))
			add_cadvisor_metric_uint("container_memory_mapped_file", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "hierarchical_memory_limit ", 26))
		{
			if (val > 1000000000000000)
				val = 0;
			add_cadvisor_metric_uint("container_spec_memory_limit_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
		else if (!strncmp(buf, "hierarchical_memsw_limit ", 25))
		{
			if (val > 1000000000000000)
				val = 0;
			add_cadvisor_metric_uint("container_spec_memory_swap_limit_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
		else if (!strncmp(buf, "total_pgfault", 13))
			add_cadvisor_metric_uint("container_memory_failures_total", val, cntid, name, image, cad_id, "type", "pgfault", kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "total_pgmajfault", 16))
			add_cadvisor_metric_uint("container_memory_failures_total", val, cntid, name, image, cad_id, "type", "pgmajfault", kubenamespace, kubepod, kubecontainer, NULL);
	}
	fclose(fd);

	snprintf(fpath, 1000, "%s/fs/cgroup/memory/%s/%s/memory.memsw.failcnt", ac->system_sysfs, prefix, cntid);

	fd = fopen(fpath, "r");
	if (!fd)
		return;

	if (!fgets(buf, 1000, fd))
	{
		fclose(fd);
		return;
	}
	fclose(fd);

	val = strtoull(buf, NULL, 10);
	add_cadvisor_metric_uint("container_memory_failcnt", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);

	snprintf(fpath, 1000, "%s/fs/cgroup/memory/%s/%s/memory.kmem.usage_in_bytes", ac->system_sysfs, prefix, cntid);
	fd = fopen(fpath, "r");
	if (!fd)
		return;

	if (!fgets(buf, 1000, fd))
	{
		fclose(fd);
		return;
	}
	fclose(fd);

	working_set += strtoull(buf, NULL, 10);
	add_cadvisor_metric_uint("container_memory_working_set_bytes", working_set, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);

	snprintf(fpath, 1000, "%s/fs/cgroup/memory/%s/%s/memory.max_usage_in_bytes", ac->system_sysfs, prefix, cntid);
	fd = fopen(fpath, "r");
	if (!fd)
		return;

	if (!fgets(buf, 1000, fd))
	{
		fclose(fd);
		return;
	}
	fclose(fd);

	val = strtoull(buf, NULL, 10);
	add_cadvisor_metric_uint("container_memory_max_usage_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);

	snprintf(fpath, 1000, "%s/fs/cgroup/memory/%s/%s/memory.usage_in_bytes", ac->system_sysfs, prefix, cntid);
	fd = fopen(fpath, "r");
	if (!fd)
		return;

	if (!fgets(buf, 1000, fd))
	{
		fclose(fd);
		return;
	}
	fclose(fd);

	val = strtoull(buf, NULL, 10);
	add_cadvisor_metric_uint("container_memory_usage_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);

	snprintf(fpath, 1000, "%s/fs/cgroup/memory/%s/%s/memory.soft_limit_in_bytes", ac->system_sysfs, prefix, cntid);
	fd = fopen(fpath, "r");
	if (!fd)
		return;

	if (!fgets(buf, 1000, fd))
	{
		fclose(fd);
		return;
	}
	fclose(fd);

	val = strtoull(buf, NULL, 10);

	if (val > 1000000000000000)
		val = 0;
	add_cadvisor_metric_uint("container_spec_memory_reservation_limit_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
}

void cgroupv2_memory_info(char *prefix, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char fpath[1000];
	char buf[1000];
	char *tmp;
	uint64_t val;
	uint64_t working_set = 0;
	uint64_t memory_usage = 0;
	snprintf(fpath, 1000, "%s/fs/cgroup/%s/%s/memory.stat", ac->system_sysfs, prefix, cntid);

	FILE *fd = fopen(fpath, "r");
	carglog(ac->cadvisor_carg, L_INFO, "cadvisor memory v2 tries open '%s': %p\n", fpath, fd);
	if (!fd)
		return;

	while(fgets(buf, 1000, fd))
	{
		tmp = buf;
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		val = strtoull(tmp, NULL, 10);
		if (!strncmp(buf, "file_mapped ", 12))
			add_cadvisor_metric_uint("container_memory_mapped_file", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "pgfault", 7))
			add_cadvisor_metric_uint("container_memory_failures_total", val, cntid, name, image, cad_id, "type", "pgfault", kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "active_anon", 11)) {
			add_cadvisor_metric_uint("container_memory_rss", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
			working_set += val;
			memory_usage += val;
		}
		else if (!strncmp(buf, "inactive_file", 13))
			add_cadvisor_metric_uint("container_memory_cache", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "active_file", 11)) {
			working_set += val;
			memory_usage += val;
		}
		else if (!strncmp(buf, "pgmajfault", 10))
			add_cadvisor_metric_uint("container_memory_failures_total", val, cntid, name, image, cad_id, "type", "pgmajfault", kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "workingset", 10))
			working_set += val;
	}
	fclose(fd);

	add_cadvisor_metric_uint("container_memory_working_set_bytes", working_set, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_uint("container_memory_usage_bytes", memory_usage, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);

	snprintf(fpath, 1000, "%s/fs/cgroup/%s/%s/memory.swap.max", ac->system_sysfs, prefix, cntid);
	carglog(ac->cadvisor_carg, L_INFO, "cadvisor memory v2 tries open '%s': %p\n", fpath, fd);
	fd = fopen(fpath, "r");
	if (!fd)
		return;

	if (!fgets(buf, 1000, fd))
	{
		fclose(fd);
		return;
	}
	fclose(fd);

	if (strncmp(buf, "max", 3)) {
		val = strtoull(buf, NULL, 10);
		add_cadvisor_metric_uint("container_spec_memory_swap_limit_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	}

	snprintf(fpath, 1000, "%s/fs/cgroup/%s/%s/memory.max", ac->system_sysfs, prefix, cntid);
	carglog(ac->cadvisor_carg, L_INFO, "cadvisor memory v2 tries open '%s': %p\n", fpath, fd);
	fd = fopen(fpath, "r");
	if (!fd)
		return;

	if (!fgets(buf, 1000, fd))
	{
		fclose(fd);
		return;
	}
	fclose(fd);

	if (strncmp(buf, "max", 3)) {
		val = strtoull(buf, NULL, 10);
		add_cadvisor_metric_uint("container_spec_memory_limit_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	}

	snprintf(fpath, 1000, "%s/fs/cgroup/%s/%s/memory.swap.current", ac->system_sysfs, prefix, cntid);
	carglog(ac->cadvisor_carg, L_INFO, "cadvisor memory v2 tries open '%s': %p\n", fpath, fd);
	fd = fopen(fpath, "r");
	if (!fd)
		return;

	if (!fgets(buf, 1000, fd))
	{
		fclose(fd);
		return;
	}
	fclose(fd);

	val = strtoull(buf, NULL, 10);
	add_cadvisor_metric_uint("container_memory_swap", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer,  NULL);

	snprintf(fpath, 1000, "%s/fs/cgroup/%s/%s/memory.events", ac->system_sysfs, prefix, cntid);
	carglog(ac->cadvisor_carg, L_INFO, "cadvisor memory v2 tries open '%s': %p\n", fpath, fd);
	fd = fopen(fpath, "r");
	if (!fd)
		return;

	while(fgets(buf, 1000, fd))
	{
		tmp = buf;
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		val = strtoull(tmp, NULL, 10);
		if (!strncmp(buf, "max", 3))
			add_cadvisor_metric_uint("container_memory_failcnt", val, cntid, name, image, cad_id, "type", "max", kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "high", 4))
			add_cadvisor_metric_uint("container_memory_failcnt", val, cntid, name, image, cad_id, "type", "high", kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "oom", 3))
			add_cadvisor_metric_uint("container_memory_failcnt", val, cntid, name, image, cad_id, "type", "oom", kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "low", 3))
			add_cadvisor_metric_uint("container_memory_failcnt", val, cntid, name, image, cad_id, "type", "low", kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "oom_kill", 8))
			add_cadvisor_metric_uint("container_memory_oom_kill", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	}
	fclose(fd);
}

void get_start_time(char *prefix, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	char fpath[1000];
	snprintf(fpath, 1000, "%s/fs/cgroup/cpu,cpuacct/%s/%s", ac->system_sysfs, prefix, cntid);
	uint64_t uptime = get_file_atime(fpath);
	if (!uptime) {
		snprintf(fpath, 1000, "%s/fs/cgroup/%s/%s", ac->system_sysfs, prefix, cntid);
		uptime = get_file_atime(fpath);
	}
	add_cadvisor_metric_uint("container_start_time_seconds", uptime, cntid, name, image, cad_id, 0, 0, kubenamespace, kubepod, kubecontainer, libvirt_id);
}

void cgroup_get_quotas(char *prefix, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char fpath[1000];
	int64_t ival;

	snprintf(fpath, 1000, "%s/fs/cgroup/cpuacct/%s/%s/cpu.shares", ac->system_sysfs, prefix, cntid);
	int rc = get_int_file(fpath, &ival);
    if (rc)
		add_cadvisor_metric_int("container_spec_cpu_shares", ival, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);

	snprintf(fpath, 1000, "%s/fs/cgroup/cpuacct/%s/%s/cpu.cfs_period_us", ac->system_sysfs, prefix, cntid);
	rc = get_int_file(fpath, &ival);
	if (rc)
		add_cadvisor_metric_int("container_spec_cpu_period", ival, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	uint64_t periods = ival;

	snprintf(fpath, 1000, "%s/fs/cgroup/cpuacct/%s/%s/cpu.cfs_quota_us", ac->system_sysfs, prefix, cntid);
	rc = get_int_file(fpath, &ival);
	if (ival < 0)
	{
		snprintf(fpath, 1000, "%s/fs/cgroup/cpu,cpuacct/cpu.cfs_quota_us", ac->system_sysfs);
		rc = get_int_file(fpath, &ival);
		if (ival < 0)
		{
			ival = sysconf( _SC_NPROCESSORS_ONLN ) * periods;
			if (ival < 0)
				ival = 0;
		}
	}

	if (rc)
		add_cadvisor_metric_int("container_spec_cpu_quota", ival, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
}

void cgroupv2_cpu_info(char *prefix, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	char fpath[1000];
	char buf[1000];
	char *tmp;
	uint64_t val;
	double dval;
	snprintf(fpath, 1000, "%s/fs/cgroup/%s/%s/cpu.stat", ac->system_sysfs, prefix, cntid);

	FILE *fd = fopen(fpath, "r");
	carglog(ac->cadvisor_carg, L_INFO, "cadvisor cpu v2 tries open '%s': %p\n", fpath, fd);
	if (!fd)
		return;

	while(fgets(buf, 1000, fd))
	{
		tmp = buf;
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
		val = strtoull(tmp, NULL, 10);
		dval = strtod(tmp, NULL);
		if (!strncmp(buf, "usage_usec", 10))
			add_cadvisor_metric_double("container_cpu_usage_seconds_total", dval * 1.0 / 1000000, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
		else if (!strncmp(buf, "user_usec", 9))
			add_cadvisor_metric_double("container_cpu_user_seconds_total", dval * 1.0 / 1000000, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
		else if (!strncmp(buf, "system_usec", 11))
			add_cadvisor_metric_double("container_cpu_system_seconds_total", dval * 1.0 / 1000000, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
		else if (!strncmp(buf, "nr_periods", 10))
			add_cadvisor_metric_double("container_cpu_schedstat_run_periods_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
		else if (!strncmp(buf, "nr_throttled", 12))
			add_cadvisor_metric_double("container_cpu_cfs_throttled_periods_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
		else if (!strncmp(buf, "throttled_usec", 14))
			add_cadvisor_metric_double("container_cpu_cfs_throttled_seconds_total", dval * 1.0 / 1000000, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
	}
	fclose(fd);

	snprintf(fpath, 1000, "%s/fs/cgroup/%s/%s/cpu.max", ac->system_sysfs, prefix, cntid);
	carglog(ac->cadvisor_carg, L_INFO, "cadvisor cpu v2 tries open '%s': %p\n", fpath, fd);
	fd = fopen(fpath, "r");
	if (!fd)
		return;

	if (!fgets(buf, 1000, fd))
	{
		fclose(fd);
		return;
	}
	fclose(fd);

	uint64_t max = 100000;
	if (strncmp(buf, "max", 3)) {
		max = strtoull(buf, NULL, 10);
	}

	uint64_t period = 100000;
	char *ptr_to_period = strstr(buf, " ");
	if (ptr_to_period)
		period = strtoull(ptr_to_period + 1, NULL, 10);

	carglog(ac->cadvisor_carg, L_INFO, "cadvisor cpu max %"PRIu64", period %"PRIu64"\n", max, period);
	add_cadvisor_metric_uint("container_spec_cpu_period", period, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
	add_cadvisor_metric_uint("container_spec_cpu_quota", max, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
}

void cgroup2_libvirt_cpu_shares(char *prefix, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id) {
	struct dirent *libvirt_cpu;

	char libvirtdir[256];
	snprintf(libvirtdir, 255, "%s/fs/cgroup/%s/%s/libvirt", ac->system_sysfs, prefix, cntid);
	DIR *rd = opendir(libvirtdir);
	carglog(ac->cadvisor_carg, L_INFO, "cadvisor cpu shares v2 tries open '%s': %p\n", libvirtdir, rd);
	if (rd)
	{
		uint64_t cpu_shares = 0;
		while((libvirt_cpu = readdir(rd)))
		{
			if (libvirt_cpu->d_name[0] == '.')
				continue;

			if (!strncmp(libvirt_cpu->d_name, "vcpu", 4))
				++cpu_shares;
		}
		add_cadvisor_metric_uint("container_spec_cpu_shares", cpu_shares, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
	}

	closedir(rd);
}

void cgroup_get_tcpudpinfo(char *tcpudpbuf, uint64_t pid, char *resource, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	char fpath[1000];
	char mname[1000];
	snprintf(fpath, 1000, "%s/%"PRIu64"/net/%s", ac->system_procfs, pid, resource);
	FILE *fd = fopen(fpath, "r");
	if (!fd)
		return;

	uint64_t val = 0;
	char *bufend;
	char *end;
	char *start;
	uint64_t rc;
	while((rc=fread(tcpudpbuf, 1, TCPUDP_NET_LENREAD, fd)))
	{
		bufend = tcpudpbuf+rc;
		start = tcpudpbuf;
		while(start < bufend)
		{
			end = strstr(start, "\n");
			if (!end)
				break;
			start = end +1;
			++val;
		}
	}
	fclose(fd);
	snprintf(mname, 1000, "container_network_%s_usage_total", resource);
	add_cadvisor_metric_uint(mname, val, cntid, name, image, cad_id, 0, 0, kubenamespace, kubepod, kubecontainer, libvirt_id);
}

void cadvisor_network_scrape(char *sysfs, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	carglog(ac->cadvisor_carg, L_INFO, "cadvisor_network_scrape sysfs %s, cntid %s, name %s, image %s, cad_id %s, kubenamespace %s, kubepod %s, kubecontainer %s\n", sysfs, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);
	char pidsdir[1000];
	DIR *ethd;
	struct dirent *ethd_entry;

	snprintf(pidsdir, 1000, "%s/fs/cgroup/pids/%s/cgroup.procs", sysfs, cad_id);

	if (!mount_ns_by_cgroup_procs(pidsdir, name))
	{
		snprintf(pidsdir, 1000, "%s/fs/cgroup/cpu,cpuacct/%s/cgroup.procs", sysfs, cad_id);
		if (!mount_ns_by_cgroup_procs(pidsdir, name)) {
			snprintf(pidsdir, 1000, "%s/fs/cgroup/%s/cgroup.procs", sysfs, cad_id);
			if (!mount_ns_by_cgroup_procs(pidsdir, name))
				return;
		}
	}

	ethd = opendir("/var/lib/alligator/nsmount/class/net/");
	if (ethd)
	{
		while((ethd_entry = readdir(ethd)))
		{
			if (ethd_entry->d_name[0] == '.')
				continue;

			cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "rx_dropped", "container_network_receive_packets_dropped_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
			cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "rx_bytes", "container_network_receive_bytes_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
			cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "tx_bytes", "container_network_transmit_bytes_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
			cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "tx_packets", "container_network_transmit_packets_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
			cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "rx_packets", "container_network_receive_packets_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
			cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "tx_errors", "container_network_transmit_errors_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
			cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "rx_errors", "container_network_receive_errors_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
			cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "tx_dropped", "container_network_transmit_packets_dropped_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
			cgroup_get_netinfo("/var/lib/alligator/nsmount", ethd_entry->d_name, "rx_dropped", "container_network_receive_packets_dropped_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
		}
		closedir(ethd);
	}

	umount("/var/lib/alligator/nsmount");

	int rc = unshare(CLONE_NEWNET);
	if (rc)
		carglog(ac->cadvisor_carg, L_ERROR, "unshare %s failed: %d\n", name, rc);

	mount_ns_by_pid("1");
}

void cadvisor_scrape(char *ifname, char *slice, char *cntid, char *name, char *image, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	carglog(ac->cadvisor_carg, L_INFO, "cadvisor_scrape search id in slice %s id: %s with name: %s\n", slice, cntid, name);
	if (!ac->cadvisor_tcpudpbuf)
		ac->cadvisor_tcpudpbuf = malloc(TCPUDP_NET_LENREAD);

	char cad_id[255];
	if (*slice != 0)
		snprintf(cad_id, 255, "/%s/%s", slice, cntid);
	else
		snprintf(cad_id, 255, "/%s", cntid);

	cadvisor_network_scrape(ac->system_sysfs, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
	if (ifname)
	{
		cgroup_get_netinfo(ac->system_sysfs, ifname, "rx_bytes", "container_network_receive_bytes_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
		cgroup_get_netinfo(ac->system_sysfs, ifname, "tx_bytes", "container_network_transmit_bytes_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
		cgroup_get_netinfo(ac->system_sysfs, ifname, "tx_packets", "container_network_transmit_packets_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
		cgroup_get_netinfo(ac->system_sysfs, ifname, "rx_packets", "container_network_receive_packets_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
		cgroup_get_netinfo(ac->system_sysfs, ifname, "tx_errors", "container_network_transmit_errors_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
		cgroup_get_netinfo(ac->system_sysfs, ifname, "rx_errors", "container_network_receive_errors_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
		cgroup_get_netinfo(ac->system_sysfs, ifname, "tx_dropped", "container_network_transmit_packets_dropped_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
		cgroup_get_netinfo(ac->system_sysfs, ifname, "rx_dropped", "container_network_receive_packets_dropped_total", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
	}



	alligator_ht* mlist_hash = get_mountpoint_list();
	alligator_ht* dlid_hash = get_disk_ids_names(cntid, name, image, cad_id, mlist_hash, kubenamespace, kubepod, kubecontainer, libvirt_id);
	uint64_t pid = cgroup_get_pid(slice, cntid);

	if (mlist_hash)
	{
		cgroup_fd_disk_part_info(slice, pid, cntid, mlist_hash, name, image, cad_id, kubenamespace, kubepod, kubecontainer);
		mlist_free(mlist_hash);
	}

	if (dlid_hash)
	{
		cgroup_get_diskinfo(dlid_hash, slice, cntid, "io_merged", "container_fs_%s_merged_total", name, image, cad_id, "writes", "reads", kubenamespace, kubepod, kubecontainer);
		cgroup_get_diskinfo(dlid_hash, slice, cntid, "io_serviced", "container_fs_%s_total", name, image, cad_id, "writes", "reads", kubenamespace, kubepod, kubecontainer);
		cgroup_get_diskinfo(dlid_hash, slice, cntid, "sectors", "container_fs_sector_%s_total", name, image, cad_id, "writes", "reads", kubenamespace, kubepod, kubecontainer);
		cgroup_get_diskinfo(dlid_hash, slice, cntid, "io_service_bytes", "container_fs_%s_bytes_total", name, image, cad_id, "writes", "reads", kubenamespace, kubepod, kubecontainer);
		cgroup_get_diskinfo(dlid_hash, slice, cntid, "io_service_time", "container_fs_%s_seconds_total", name, image, cad_id, "write", "read", kubenamespace, kubepod, kubecontainer);
		cgroup_get_diskinfo_total(dlid_hash, slice, cntid, "io_service_time", "container_fs_io_time_seconds_total", name, image, cad_id, kubenamespace, kubepod, kubecontainer);
		cgroup_get_diskinfo_total(dlid_hash, slice, cntid, "io_queued", "container_fs_io_current", name, image, cad_id, kubenamespace, kubepod, kubecontainer);

		cgroup_get_tcpudpinfo(ac->cadvisor_tcpudpbuf, pid, "tcp", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
		cgroup_get_tcpudpinfo(ac->cadvisor_tcpudpbuf, pid, "udp", cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);

		cgroup2_get_diskinfo(ac->cadvisor_carg, dlid_hash, slice, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id, "io.stat", "container_fs_reads_bytes_total", "container_fs_writes_bytes_total", "container_fs_reads_total", "container_fs_writes_total");
		cgroup2_get_diskinfo(ac->cadvisor_carg, dlid_hash, slice, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id, "io.max", "container_spec_fs_reads_rate_limit_bytes", "container_spec_fs_writes_rate_limit_bytes", "container_spec_fs_reads_rate_limit_total", "container_spec_fs_writes_rate_limit_total");

		dlid_free(dlid_hash);
	}

	cgroup_get_cpuinfo(slice, cntid, "cpu.stat", name, image, cad_id, kubenamespace, kubepod, kubecontainer);
	cgroup_get_cpuinfo(slice, cntid, "cpuacct.stat", name, image, cad_id, kubenamespace, kubepod, kubecontainer);
	cgroupv2_cpu_info(slice, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
	cgroup2_libvirt_cpu_shares(slice, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
	cgroup_get_cpuacctinfo(slice, cntid, "cpuacct.usage_percpu", "container_cpu_usage_seconds_total", name, image, cad_id, kubenamespace, kubepod, kubecontainer);
	cgroup_get_quotas(slice, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);
	cgroup_cpu_schedstats_info(slice, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);

	cgroup_memory_info(slice, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);
	if (!libvirt_id)
		cgroupv2_memory_info(slice, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);
	get_start_time(slice, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);

	r_time now = setrtime();
	uint64_t last_seen = now.sec;
	add_cadvisor_metric_uint("container_last_seen", last_seen, cntid, name, image, cad_id, 0, 0, kubenamespace, kubepod, kubecontainer, libvirt_id);
}
#endif
