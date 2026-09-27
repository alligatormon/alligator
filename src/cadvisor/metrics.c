#ifdef __linux__
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <mntent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <linux/sched.h>
#include "main.h"
#include "cadvisor/metrics.h"
#include "cadvisor/parsers.h"
#include "cadvisor/ns.h"
#include <dirent.h>
#include <ctype.h>
#include "metric/metric_types.h"
#include "metric/namespace.h"
#include <sys/mount.h>
#include "common/logs.h"
#include <sys/vfs.h>
#include <linux/magic.h>
#define PATH_SIZE 1000
int unshare(int flags);

extern aconf *ac;

static void cadvisor_fopen_fail(const char *path)
{
	int pri = (errno == ENOENT || errno == ENOTDIR) ? L_DEBUG : L_ERROR;
	carglog(ac->cadvisor_carg, pri, "cadvisor fopen %s: %s\n", path, strerror(errno));
}

typedef struct disk_list_id
{
	char id[41];
	char devname[1000];
	uint64_t dstats[11];
	int have_dstats;

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
	if (!fd) {
		cadvisor_fopen_fail(fpath);
		return 0;
	}

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

void cadvisor_register_metric_families(context_arg *carg)
{
	if (!carg)
		return;

	/* CPU (cgroup cpuacct / cpu.stat). */
	namespace_metric_family_set(NULL, carg, "container_cpu_cfs_periods_total", METRIC_TYPE_COUNTER, "Number of elapsed enforcement period intervals.");
	namespace_metric_family_set(NULL, carg, "container_cpu_cfs_throttled_periods_total", METRIC_TYPE_COUNTER, "Number of throttled period intervals.");
	namespace_metric_family_set(NULL, carg, "container_cpu_cfs_throttled_seconds_total", METRIC_TYPE_COUNTER, "Total time spent throttled in seconds.");
	namespace_metric_family_set(NULL, carg, "container_cpu_system_seconds_total", METRIC_TYPE_COUNTER, "Cumulative system CPU time consumed in seconds.");
	namespace_metric_family_set(NULL, carg, "container_cpu_usage_seconds_total", METRIC_TYPE_COUNTER, "Cumulative CPU time consumed in seconds.");
	namespace_metric_family_set(NULL, carg, "container_cpu_user_seconds_total", METRIC_TYPE_COUNTER, "Cumulative user CPU time consumed in seconds.");
	namespace_metric_family_set(NULL, carg, "container_cpu_all_seconds_total", METRIC_TYPE_COUNTER, "Cumulative CPU time consumed in seconds (all modes).");
	namespace_metric_family_set(NULL, carg, "container_cpu_schedstat_run_periods_total", METRIC_TYPE_COUNTER, "Cumulative number of scheduling run periods.");
	namespace_metric_family_set(NULL, carg, "container_cpu_schedstat_runqueue_seconds_total", METRIC_TYPE_COUNTER, "Cumulative time spent waiting on the runqueue in seconds.");
	namespace_metric_family_set(NULL, carg, "container_cpu_schedstat_run_seconds_total", METRIC_TYPE_COUNTER, "Cumulative time spent running on the CPU in seconds.");
	namespace_metric_family_set(NULL, carg, "container_spec_cpu_shares", METRIC_TYPE_GAUGE, "CPU shares weight for the container.");

	/* Block I/O (blkio cgroup). */
	namespace_metric_family_set(NULL, carg, "container_fs_io_time_seconds_total", METRIC_TYPE_COUNTER, "Cumulative time spent on filesystem I/O in seconds.");
	namespace_metric_family_set(NULL, carg, "container_fs_io_time_weighted_seconds_total", METRIC_TYPE_COUNTER, "Cumulative weighted time spent on filesystem I/O in seconds.");
	namespace_metric_family_set(NULL, carg, "container_fs_read_seconds_total", METRIC_TYPE_COUNTER, "Cumulative time spent on filesystem reads in seconds.");
	namespace_metric_family_set(NULL, carg, "container_fs_reads_merged_total", METRIC_TYPE_COUNTER, "Cumulative count of filesystem reads merged.");
	namespace_metric_family_set(NULL, carg, "container_fs_sector_reads_total", METRIC_TYPE_COUNTER, "Cumulative count of filesystem sectors read.");
	namespace_metric_family_set(NULL, carg, "container_fs_sector_writes_total", METRIC_TYPE_COUNTER, "Cumulative count of filesystem sectors written.");
	namespace_metric_family_set(NULL, carg, "container_fs_write_seconds_total", METRIC_TYPE_COUNTER, "Cumulative time spent on filesystem writes in seconds.");
	namespace_metric_family_set(NULL, carg, "container_fs_writes_merged_total", METRIC_TYPE_COUNTER, "Cumulative count of filesystem writes merged.");
	namespace_metric_family_set(NULL, carg, "container_fs_io_current", METRIC_TYPE_GAUGE, "Number of I/O operations currently in progress.");
	namespace_metric_family_set(NULL, carg, "container_fs_reads_bytes_total", METRIC_TYPE_COUNTER, "Cumulative count of bytes read from the filesystem.");
	namespace_metric_family_set(NULL, carg, "container_fs_writes_bytes_total", METRIC_TYPE_COUNTER, "Cumulative count of bytes written to the filesystem.");
	namespace_metric_family_set(NULL, carg, "container_fs_reads_total", METRIC_TYPE_COUNTER, "Cumulative count of filesystem read operations.");
	namespace_metric_family_set(NULL, carg, "container_fs_writes_total", METRIC_TYPE_COUNTER, "Cumulative count of filesystem write operations.");
	namespace_metric_family_set(NULL, carg, "container_fs_inodes_free", METRIC_TYPE_GAUGE, "Number of available inodes on the filesystem.");
	namespace_metric_family_set(NULL, carg, "container_fs_inodes_usage", METRIC_TYPE_GAUGE, "Number of inodes in use on the filesystem.");
	namespace_metric_family_set(NULL, carg, "container_fs_inodes_total", METRIC_TYPE_GAUGE, "Total number of inodes on the filesystem.");
	namespace_metric_family_set(NULL, carg, "container_fs_limit_bytes", METRIC_TYPE_GAUGE, "Filesystem size limit in bytes.");
	namespace_metric_family_set(NULL, carg, "container_fs_usage_bytes", METRIC_TYPE_GAUGE, "Number of bytes used on the filesystem.");
	namespace_metric_family_set(NULL, carg, "container_fs_free_bytes", METRIC_TYPE_GAUGE, "Number of bytes available on the filesystem.");
	namespace_metric_family_set(NULL, carg, "container_spec_fs_reads_rate_limit_bytes", METRIC_TYPE_GAUGE, "Filesystem read rate limit in bytes per second.");
	namespace_metric_family_set(NULL, carg, "container_spec_fs_writes_rate_limit_bytes", METRIC_TYPE_GAUGE, "Filesystem write rate limit in bytes per second.");
	namespace_metric_family_set(NULL, carg, "container_spec_fs_reads_rate_limit_total", METRIC_TYPE_COUNTER, "Filesystem read IOPS limit.");
	namespace_metric_family_set(NULL, carg, "container_spec_fs_writes_rate_limit_total", METRIC_TYPE_COUNTER, "Filesystem write IOPS limit.");
	namespace_metric_family_set(NULL, carg, "container_spec_fs_total_rate_limit_bytes", METRIC_TYPE_GAUGE, "Combined filesystem I/O rate limit in bytes per second.");
	namespace_metric_family_set(NULL, carg, "container_spec_fs_total_rate_limit_total", METRIC_TYPE_COUNTER, "Combined filesystem I/O operations limit.");

	/* Container lifecycle / CPU limits. */
	namespace_metric_family_set(NULL, carg, "container_last_seen", METRIC_TYPE_GAUGE, "Last time the container was seen by the scraper (unix timestamp).");
	namespace_metric_family_set(NULL, carg, "container_spec_cpu_period", METRIC_TYPE_GAUGE, "CPU CFS scheduler period in microseconds.");
	namespace_metric_family_set(NULL, carg, "container_spec_cpu_quota", METRIC_TYPE_GAUGE, "CPU CFS scheduler quota in microseconds.");
	namespace_metric_family_set(NULL, carg, "container_start_time_seconds", METRIC_TYPE_GAUGE, "Start time of the container since unix epoch in seconds.");

	/* Memory (cgroup memory). */
	namespace_metric_family_set(NULL, carg, "container_memory_cache", METRIC_TYPE_GAUGE, "Number of bytes of page cache memory.");
	namespace_metric_family_set(NULL, carg, "container_memory_failcnt", METRIC_TYPE_COUNTER, "Cumulative count of memory allocation failures.");
	namespace_metric_family_set(NULL, carg, "container_memory_failures_total", METRIC_TYPE_COUNTER, "Cumulative count of memory failures by type.");
	namespace_metric_family_set(NULL, carg, "container_memory_mapped_file", METRIC_TYPE_GAUGE, "Size of memory mapped files in bytes.");
	namespace_metric_family_set(NULL, carg, "container_memory_rss", METRIC_TYPE_GAUGE, "Size of RSS memory in bytes.");
	namespace_metric_family_set(NULL, carg, "container_memory_swap", METRIC_TYPE_GAUGE, "Container swap usage in bytes.");
	namespace_metric_family_set(NULL, carg, "container_memory_usage_bytes", METRIC_TYPE_GAUGE, "Current memory usage in bytes.");
	namespace_metric_family_set(NULL, carg, "container_memory_working_set_bytes", METRIC_TYPE_GAUGE, "Current working set size in bytes.");
	namespace_metric_family_set(NULL, carg, "container_memory_max_usage_bytes", METRIC_TYPE_GAUGE, "Maximum memory usage recorded in bytes.");
	namespace_metric_family_set(NULL, carg, "container_memory_oom_kill", METRIC_TYPE_COUNTER, "Cumulative count of OOM kills.");
	namespace_metric_family_set(NULL, carg, "container_spec_memory_limit_bytes", METRIC_TYPE_GAUGE, "Memory limit for the container in bytes.");
	namespace_metric_family_set(NULL, carg, "container_spec_memory_swap_limit_bytes", METRIC_TYPE_GAUGE, "Memory plus swap limit for the container in bytes.");
	namespace_metric_family_set(NULL, carg, "container_spec_memory_reservation_limit_bytes", METRIC_TYPE_GAUGE, "Memory reservation limit for the container in bytes.");

	/* Process / task state. */
	namespace_metric_family_set(NULL, carg, "container_tasks_state", METRIC_TYPE_GAUGE, "Number of tasks in a given state.");
	namespace_metric_family_set(NULL, carg, "container_file_descriptors", METRIC_TYPE_GAUGE, "Number of open file descriptors.");
	namespace_metric_family_set(NULL, carg, "container_processes", METRIC_TYPE_GAUGE, "Number of processes in the container.");
	namespace_metric_family_set(NULL, carg, "container_threads", METRIC_TYPE_GAUGE, "Number of threads in the container.");
	namespace_metric_family_set(NULL, carg, "container_threads_max", METRIC_TYPE_GAUGE, "Maximum number of threads allowed (0 means unlimited).");
	namespace_metric_family_set(NULL, carg, "container_sockets", METRIC_TYPE_GAUGE, "Number of open sockets in the container.");
	namespace_metric_family_set(NULL, carg, "container_ulimits_soft", METRIC_TYPE_GAUGE, "Soft ulimit values for the first process in the container.");

	/* CPU burst (cgroup v2). */
	namespace_metric_family_set(NULL, carg, "container_cpu_cfs_burst_periods_total", METRIC_TYPE_COUNTER, "Number of CFS burst periods.");
	namespace_metric_family_set(NULL, carg, "container_cpu_cfs_burst_seconds_total", METRIC_TYPE_COUNTER, "Cumulative CFS burst time in seconds.");

	/* Extended memory / events / NUMA / hugetlb. */
	namespace_metric_family_set(NULL, carg, "container_memory_kernel_usage", METRIC_TYPE_GAUGE, "Kernel memory usage in bytes.");
	namespace_metric_family_set(NULL, carg, "container_memory_total_active_file_bytes", METRIC_TYPE_GAUGE, "Active file cache memory in bytes.");
	namespace_metric_family_set(NULL, carg, "container_memory_total_inactive_file_bytes", METRIC_TYPE_GAUGE, "Inactive file cache memory in bytes.");
	namespace_metric_family_set(NULL, carg, "container_memory_file_dirty_bytes", METRIC_TYPE_GAUGE, "Dirty file cache memory in bytes.");
	namespace_metric_family_set(NULL, carg, "container_memory_file_writeback_bytes", METRIC_TYPE_GAUGE, "Writeback file cache memory in bytes.");
	namespace_metric_family_set(NULL, carg, "container_memory_pgscan_total", METRIC_TYPE_COUNTER, "Number of pages scanned by the reclaim.");
	namespace_metric_family_set(NULL, carg, "container_memory_pgsteal_total", METRIC_TYPE_COUNTER, "Number of pages reclaimed.");
	namespace_metric_family_set(NULL, carg, "container_memory_workingset_refault_file_total", METRIC_TYPE_COUNTER, "Number of refaults of previously evicted file pages.");
	namespace_metric_family_set(NULL, carg, "container_memory_workingset_refault_anon_total", METRIC_TYPE_COUNTER, "Number of refaults of previously evicted anonymous pages.");
	namespace_metric_family_set(NULL, carg, "container_memory_events_low_total", METRIC_TYPE_COUNTER, "Number of times the memory usage went under the low threshold.");
	namespace_metric_family_set(NULL, carg, "container_memory_events_high_total", METRIC_TYPE_COUNTER, "Number of times the memory usage hit the high limit.");
	namespace_metric_family_set(NULL, carg, "container_memory_events_max_total", METRIC_TYPE_COUNTER, "Number of times the memory usage hit the max limit.");
	namespace_metric_family_set(NULL, carg, "container_memory_events_oom_group_kill_total", METRIC_TYPE_COUNTER, "Number of times a group OOM kill was performed.");
	namespace_metric_family_set(NULL, carg, "container_oom_events_total", METRIC_TYPE_COUNTER, "Number of OOM events observed.");
	namespace_metric_family_set(NULL, carg, "container_memory_migrate", METRIC_TYPE_GAUGE, "Whether memory migrate is enabled for the cpuset.");
	namespace_metric_family_set(NULL, carg, "container_memory_numa_pages", METRIC_TYPE_GAUGE, "NUMA page counts by type, scope and node.");
	namespace_metric_family_set(NULL, carg, "container_hugetlb_usage_bytes", METRIC_TYPE_GAUGE, "Current hugetlb usage in bytes by pagesize.");
	namespace_metric_family_set(NULL, carg, "container_hugetlb_max_usage_bytes", METRIC_TYPE_GAUGE, "Maximum hugetlb usage in bytes by pagesize.");
	namespace_metric_family_set(NULL, carg, "container_hugetlb_failcnt", METRIC_TYPE_COUNTER, "Number of hugetlb allocation failures by pagesize.");

	/* Block I/O extras. */
	namespace_metric_family_set(NULL, carg, "container_blkio_device_usage_total", METRIC_TYPE_COUNTER, "Blkio bytes by device major/minor and operation.");
	namespace_metric_family_set(NULL, carg, "container_fs_io_cost_usage_seconds_total", METRIC_TYPE_COUNTER, "cgroup v2 io.cost usage time in seconds.");
	namespace_metric_family_set(NULL, carg, "container_fs_io_cost_wait_seconds_total", METRIC_TYPE_COUNTER, "cgroup v2 io.cost wait time in seconds.");
	namespace_metric_family_set(NULL, carg, "container_fs_io_cost_indebt_seconds_total", METRIC_TYPE_COUNTER, "cgroup v2 io.cost indebt time in seconds.");
	namespace_metric_family_set(NULL, carg, "container_fs_io_cost_indelay_seconds_total", METRIC_TYPE_COUNTER, "cgroup v2 io.cost indelay time in seconds.");

	/* Container PSI. */
	namespace_metric_family_set(NULL, carg, "container_pressure_cpu_waiting_seconds_total", METRIC_TYPE_COUNTER, "Container CPU PSI some total in seconds.");
	namespace_metric_family_set(NULL, carg, "container_pressure_cpu_stalled_seconds_total", METRIC_TYPE_COUNTER, "Container CPU PSI full total in seconds.");
	namespace_metric_family_set(NULL, carg, "container_pressure_memory_waiting_seconds_total", METRIC_TYPE_COUNTER, "Container memory PSI some total in seconds.");
	namespace_metric_family_set(NULL, carg, "container_pressure_memory_stalled_seconds_total", METRIC_TYPE_COUNTER, "Container memory PSI full total in seconds.");
	namespace_metric_family_set(NULL, carg, "container_pressure_io_waiting_seconds_total", METRIC_TYPE_COUNTER, "Container IO PSI some total in seconds.");
	namespace_metric_family_set(NULL, carg, "container_pressure_io_stalled_seconds_total", METRIC_TYPE_COUNTER, "Container IO PSI full total in seconds.");

	namespace_metric_family_set(NULL, carg, "container_perf_events_total", METRIC_TYPE_COUNTER, "Scaled cgroup perf event count.");
	namespace_metric_family_set(NULL, carg, "container_perf_events_scaling_ratio", METRIC_TYPE_GAUGE, "Perf event time_running / time_enabled.");
	namespace_metric_family_set(NULL, carg, "container_perf_uncore_events_total", METRIC_TYPE_COUNTER, "Scaled uncore perf event count for the root cgroup.");
	namespace_metric_family_set(NULL, carg, "container_perf_uncore_events_scaling_ratio", METRIC_TYPE_GAUGE, "Uncore perf event time_running / time_enabled.");
	namespace_metric_family_set(NULL, carg, "container_memory_bandwidth_bytes", METRIC_TYPE_GAUGE, "Resctrl MBM total bytes for the container.");
	namespace_metric_family_set(NULL, carg, "container_memory_bandwidth_local_bytes", METRIC_TYPE_GAUGE, "Resctrl MBM local bytes for the container.");

	/* Network (cgroup net_cls / per-interface stats). */
	namespace_metric_family_set(NULL, carg, "container_network_receive_bytes_total", METRIC_TYPE_COUNTER, "Cumulative count of bytes received.");
	namespace_metric_family_set(NULL, carg, "container_network_receive_errors_total", METRIC_TYPE_COUNTER, "Cumulative count of errors encountered while receiving.");
	namespace_metric_family_set(NULL, carg, "container_network_receive_packets_dropped_total", METRIC_TYPE_COUNTER, "Cumulative count of inbound packets dropped.");
	namespace_metric_family_set(NULL, carg, "container_network_receive_packets_total", METRIC_TYPE_COUNTER, "Cumulative count of packets received.");
	namespace_metric_family_set(NULL, carg, "container_network_transmit_bytes_total", METRIC_TYPE_COUNTER, "Cumulative count of bytes transmitted.");
	namespace_metric_family_set(NULL, carg, "container_network_transmit_errors_total", METRIC_TYPE_COUNTER, "Cumulative count of errors encountered while transmitting.");
	namespace_metric_family_set(NULL, carg, "container_network_transmit_packets_dropped_total", METRIC_TYPE_COUNTER, "Cumulative count of outbound packets dropped.");
	namespace_metric_family_set(NULL, carg, "container_network_transmit_packets_total", METRIC_TYPE_COUNTER, "Cumulative count of packets transmitted.");
	namespace_metric_family_set(NULL, carg, "container_network_tcp_usage_total", METRIC_TYPE_GAUGE, "Number of TCP sockets in use.");
	namespace_metric_family_set(NULL, carg, "container_network_udp_usage_total", METRIC_TYPE_GAUGE, "Number of UDP sockets in use.");
	namespace_metric_family_set(NULL, carg, "container_network_tcp6_usage_total", METRIC_TYPE_GAUGE, "Number of TCPv6 sockets in use.");
	namespace_metric_family_set(NULL, carg, "container_network_udp6_usage_total", METRIC_TYPE_GAUGE, "Number of UDPv6 sockets in use.");
	namespace_metric_family_set(NULL, carg, "container_network_advance_tcp_stats_total", METRIC_TYPE_GAUGE, "Advanced TCP SNMP stats from /proc/net/snmp.");
}

void add_cadvisor_metric_uint(char *mname, uint64_t val, char *cntid, char *name, char *image, char *cad_id, char *name1, char *value1, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	alligator_ht *hash = alligator_ht_init(NULL);
	labels_hash_insert_nocache(hash, "name", name);
	if (image)
		labels_hash_insert_nocache(hash, "image", image);
	if (kubenamespace)
		labels_hash_insert_nocache(hash, "namespace", kubenamespace);
	if (kubepod)
		labels_hash_insert_nocache(hash, "pod_name", kubepod);
	if (kubecontainer)
		labels_hash_insert_nocache(hash, "container_name", kubecontainer);
	if (libvirt_id)
		labels_hash_insert_nocache(hash, "libvirt_id", libvirt_id);
	if (name1 && value1)
		labels_hash_insert_nocache(hash, name1, value1);
	labels_hash_insert_nocache(hash, "id", cad_id);

	carglog(ac->cadvisor_carg, L_TRACE, "%s:%s:%s:%s:%s:%s:%s:%s:%s %"PRIu64"\n", mname ? mname : "", cad_id ? cad_id : "", cntid ? cntid : "", name1 ? name1 : "", value1 ? value1 : "", kubenamespace ? kubenamespace : "", kubepod ? kubepod : "", kubecontainer ? kubecontainer : "", libvirt_id ? libvirt_id : "", val);

	metric_add(mname, hash, &val, DATATYPE_UINT, ac->cadvisor_carg);
}

void add_cadvisor_metric_int(char *mname, int64_t val, char *cntid, char *name, char *image, char *cad_id, char *name1, char *value1, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	alligator_ht *hash = alligator_ht_init(NULL);
	labels_hash_insert_nocache(hash, "name", name);
	if (image)
		labels_hash_insert_nocache(hash, "image", image);
	if (kubenamespace)
		labels_hash_insert_nocache(hash, "namespace", kubenamespace);
	if (kubepod)
		labels_hash_insert_nocache(hash, "pod_name", kubepod);
	if (kubecontainer)
		labels_hash_insert_nocache(hash, "container_name", kubecontainer);
	if (libvirt_id)
		labels_hash_insert_nocache(hash, "libvirt_id", libvirt_id);
	if (name1 && value1)
		labels_hash_insert_nocache(hash, name1, value1);
	labels_hash_insert_nocache(hash, "id", cad_id);

	carglog(ac->cadvisor_carg, L_TRACE, "%s:%s:%s:%s:%s %"PRId64"\n", mname, cad_id, cntid, name1, value1, val);

	metric_add(mname, hash, &val, DATATYPE_INT, ac->cadvisor_carg);
}

void add_cadvisor_metric_double(char *mname, double val, char *cntid, char *name, char *image, char *cad_id, char *name1, char *value1, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	alligator_ht *hash = alligator_ht_init(NULL);
	labels_hash_insert_nocache(hash, "name", name);
	if (image)
		labels_hash_insert_nocache(hash, "image", image);
	if (kubenamespace)
		labels_hash_insert_nocache(hash, "namespace", kubenamespace);
	if (kubepod)
		labels_hash_insert_nocache(hash, "pod_name", kubepod);
	if (kubecontainer)
		labels_hash_insert_nocache(hash, "container_name", kubecontainer);
	if (libvirt_id)
		labels_hash_insert_nocache(hash, "libvirt_id", libvirt_id);
	if (name1 && value1)
		labels_hash_insert_nocache(hash, name1, value1);
	labels_hash_insert_nocache(hash, "id", cad_id);

	carglog(ac->cadvisor_carg, L_TRACE, "%s:%s:%s:%s:%s %lf\n", mname, cad_id, cntid, name1, value1, val);

	metric_add(mname, hash, &val, DATATYPE_DOUBLE, ac->cadvisor_carg);
}


static alligator_ht *cadvisor_labels_build(char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id, const cadvisor_label_pair *extra, size_t nextra)
{
	alligator_ht *hash = alligator_ht_init(NULL);
	size_t i;
	labels_hash_insert_nocache(hash, "name", name);
	if (image)
		labels_hash_insert_nocache(hash, "image", image);
	if (kubenamespace)
		labels_hash_insert_nocache(hash, "namespace", kubenamespace);
	if (kubepod)
		labels_hash_insert_nocache(hash, "pod_name", kubepod);
	if (kubecontainer)
		labels_hash_insert_nocache(hash, "container_name", kubecontainer);
	if (libvirt_id)
		labels_hash_insert_nocache(hash, "libvirt_id", libvirt_id);
	for (i = 0; i < nextra; ++i) {
		if (extra[i].name && extra[i].value)
			labels_hash_insert_nocache(hash, (char *)extra[i].name, (char *)extra[i].value);
	}
	labels_hash_insert_nocache(hash, "id", cad_id);
	return hash;
}

void add_cadvisor_metric_labels_uint(char *mname, uint64_t val, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id, const cadvisor_label_pair *extra, size_t nextra)
{
	alligator_ht *hash = cadvisor_labels_build(name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id, extra, nextra);
	(void)cntid;
	metric_add(mname, hash, &val, DATATYPE_UINT, ac->cadvisor_carg);
}

void add_cadvisor_metric_labels_int(char *mname, int64_t val, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id, const cadvisor_label_pair *extra, size_t nextra)
{
	alligator_ht *hash = cadvisor_labels_build(name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id, extra, nextra);
	(void)cntid;
	metric_add(mname, hash, &val, DATATYPE_INT, ac->cadvisor_carg);
}

void add_cadvisor_metric_labels_double(char *mname, double val, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id, const cadvisor_label_pair *extra, size_t nextra)
{
	alligator_ht *hash = cadvisor_labels_build(name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id, extra, nextra);
	(void)cntid;
	metric_add(mname, hash, &val, DATATYPE_DOUBLE, ac->cadvisor_carg);
}

void cgroup_emit_netinfo_stats(const network_if_stats *st, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	char *ifname;

	if (!st || !st->valid)
		return;

	ifname = (char *)st->ifname;

	add_cadvisor_metric_uint("container_network_receive_bytes_total", st->rx_bytes, cntid, name, image, cad_id, "interface", ifname, kubenamespace, kubepod, kubecontainer, libvirt_id);
	add_cadvisor_metric_uint("container_network_transmit_bytes_total", st->tx_bytes, cntid, name, image, cad_id, "interface", ifname, kubenamespace, kubepod, kubecontainer, libvirt_id);
	add_cadvisor_metric_uint("container_network_transmit_packets_total", st->tx_packets, cntid, name, image, cad_id, "interface", ifname, kubenamespace, kubepod, kubecontainer, libvirt_id);
	add_cadvisor_metric_uint("container_network_receive_packets_total", st->rx_packets, cntid, name, image, cad_id, "interface", ifname, kubenamespace, kubepod, kubecontainer, libvirt_id);
	add_cadvisor_metric_uint("container_network_transmit_errors_total", st->tx_errors, cntid, name, image, cad_id, "interface", ifname, kubenamespace, kubepod, kubecontainer, libvirt_id);
	add_cadvisor_metric_uint("container_network_receive_errors_total", st->rx_errors, cntid, name, image, cad_id, "interface", ifname, kubenamespace, kubepod, kubecontainer, libvirt_id);
	add_cadvisor_metric_uint("container_network_transmit_packets_dropped_total", st->tx_dropped, cntid, name, image, cad_id, "interface", ifname, kubenamespace, kubepod, kubecontainer, libvirt_id);
	add_cadvisor_metric_uint("container_network_receive_packets_dropped_total", st->rx_dropped, cntid, name, image, cad_id, "interface", ifname, kubenamespace, kubepod, kubecontainer, libvirt_id);
}

int cadvisor_netlink_emit_cb(const network_if_stats *st, void *arg)
{
	cadvisor_net_emit_ctx *ctx = arg;

	cgroup_emit_netinfo_stats(st, ctx->cntid, ctx->name, ctx->image, ctx->cad_id, ctx->kubenamespace, ctx->kubepod, ctx->kubecontainer, ctx->libvirt_id);
	return 0;
}

int mlist_hash_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((mnt_list*)obj)->mountpoint;
	return strcmp(s1, s2);
}

alligator_ht* get_disk_ids_names(alligator_ht* mlist_hash)
{
	char buf[PATH_SIZE];
	char mountpoint[1000];
	char devname[1000];
	size_t devsize;
	char *tmp;
	strlcpy(devname, "/dev/", 6);

	FILE *fd;
	char diskstatspath[256];
	snprintf(diskstatspath, 255, "%s/diskstats", ac->system_procfs);
	fd = fopen(diskstatspath, "r");
	if (!fd) {
		carglog(ac->cadvisor_carg, L_ERROR, "cadvisor fopen %s: %s\n", diskstatspath, strerror(errno));
		return NULL;
	}

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


		for (uint8_t j = 0; j < 11; ++j)
		{
			tmp += strcspn(tmp, " \t");
			tmp += strspn(tmp, " \t");

			dlid->dstats[j] = strtoull(tmp, NULL, 10);
		}
		dlid->have_dstats = 1;
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

static int cadvisor_dev_of_path(const char *path, int use_rdev, uint64_t *maj, uint64_t *min)
{
	struct stat st;
	dev_t dev;

	if (!path || stat(path, &st) != 0)
		return 0;
	dev = use_rdev ? st.st_rdev : st.st_dev;
	if (major(dev) == 0 && minor(dev) == 0)
		return 0;
	*maj = major(dev);
	*min = minor(dev);
	return 1;
}

static disk_list_id *cadvisor_rootfs_disk(uint64_t pid, alligator_ht *dlid_hash)
{
	char mounts[256];
	char id[41];
	FILE *fp;
	struct mntent *fs;
	uint64_t maj = 0, min = 0;
	int found = 0;
	uint32_t id_hash;

	if (!pid || !dlid_hash)
		return NULL;

	snprintf(mounts, sizeof(mounts), "%s/%"PRIu64"/mounts", ac->system_procfs, pid);
	fp = setmntent(mounts, "r");
	if (fp) {
		while ((fs = getmntent(fp)) != NULL) {
			if (strcmp(fs->mnt_dir, "/"))
				continue;
			if (!strncmp(fs->mnt_fsname, "/dev/", 5))
				found = cadvisor_dev_of_path(fs->mnt_fsname, 1, &maj, &min);
			else if (!strcmp(fs->mnt_type, "overlay")) {
				char upper[PATH_SIZE];
				if (cadvisor_overlay_upperdir(fs->mnt_opts, upper, sizeof(upper)))
					found = cadvisor_dev_of_path(upper, 0, &maj, &min);
			}
			break;
		}
		endmntent(fp);
	}

	if (!found) {
		char root[256];
		snprintf(root, sizeof(root), "%s/%"PRIu64"/root", ac->system_procfs, pid);
		found = cadvisor_dev_of_path(root, 0, &maj, &min);
	}
	if (!found)
		return NULL;

	snprintf(id, sizeof(id), "%"PRIu64":%"PRIu64, maj, min);
	id_hash = tommy_strhash_u32(0, id);
	return alligator_ht_search(dlid_hash, dlid_hash_compare, id, id_hash);
}

static void cadvisor_emit_diskstat_uint(const char *mname, uint64_t val, int keep_zero, disk_list_id *dlid, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	if (!val && !keep_zero)
		return;
	add_cadvisor_metric_uint((char *)mname, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);
}

/* /proc/diskstats time columns are milliseconds, not nanoseconds. */
static double cadvisor_diskstats_ms_to_seconds(uint64_t ms)
{
	return (double)ms / 1000.0;
}

static void cadvisor_emit_diskstat_seconds(const char *mname, uint64_t raw, disk_list_id *dlid, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	if (!raw)
		return;
	add_cadvisor_metric_double((char *)mname, cadvisor_diskstats_ms_to_seconds(raw), cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);
}

/* cAdvisor attaches /proc/diskstats to the container rootfs device only.
 * cgroup v1 already has merged/sectors/service time/queued from blkio, so the
 * filesystem half there is weighted time plus in-flight I/O. cgroup v2 io.stat
 * has no time field, so the rootfs device supplies read and write seconds.
 * That row is the whole LV: every container whose / sits on it shares it. */
static void cadvisor_emit_rootfs_diskstats(disk_list_id *dlid, int cgroupver, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	if (!dlid || !dlid->have_dstats)
		return;

	cadvisor_emit_diskstat_uint("container_fs_io_current", dlid->dstats[8], 1, dlid, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
	cadvisor_emit_diskstat_seconds("container_fs_io_time_weighted_seconds_total", dlid->dstats[10], dlid, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);

	if (cgroupver != 2)
		return;

	cadvisor_emit_diskstat_uint("container_fs_reads_merged_total", dlid->dstats[1], 0, dlid, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
	cadvisor_emit_diskstat_uint("container_fs_sector_reads_total", dlid->dstats[2], 0, dlid, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
	cadvisor_emit_diskstat_seconds("container_fs_read_seconds_total", dlid->dstats[3], dlid, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
	cadvisor_emit_diskstat_uint("container_fs_writes_merged_total", dlid->dstats[5], 0, dlid, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
	cadvisor_emit_diskstat_uint("container_fs_sector_writes_total", dlid->dstats[6], 0, dlid, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
	cadvisor_emit_diskstat_seconds("container_fs_write_seconds_total", dlid->dstats[7], dlid, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
	cadvisor_emit_diskstat_seconds("container_fs_io_time_seconds_total", dlid->dstats[9], dlid, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id);
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
	if (!fd) {
		cadvisor_fopen_fail(buf);
		return;
	}

	while(fgets(readbuf, PATH_SIZE, fd))
	{
		size_t read_size = strcspn(readbuf, " \t");
		strlcpy(disk_id, readbuf, read_size+1);

		tmp = readbuf + read_size;
		tmp += strspn(tmp, " \t");
		char *opname = NULL;
		if (!strncmp(tmp, "Write", 5)) {
			snprintf(mname, 255, mname_template, wrname);
			opname = "Write";
		} else if (!strncmp(tmp, "Read", 4)) {
			snprintf(mname, 255, mname_template, rdname);
			opname = "Read";
		} else
			continue;

		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");

		uint64_t val = strtoull(tmp, NULL, 10);
		int is_seconds = (strstr(mname, "_seconds_total") != NULL);
		int is_bytes = (strstr(mname, "_bytes_total") != NULL);

		uint32_t id_hash = tommy_strhash_u32(0, disk_id);
		disk_list_id *dlid = alligator_ht_search(dlid_hash, dlid_hash_compare, disk_id, id_hash);
		if (!dlid)
			continue;

		if (is_bytes && opname) {
			char major[16], minor[16];
			char *colon = strchr(disk_id, ':');
			if (colon) {
				size_t majlen = (size_t)(colon - disk_id);
				if (majlen >= sizeof(major))
					majlen = sizeof(major) - 1;
				memcpy(major, disk_id, majlen);
				major[majlen] = '\0';
				strlcpy(minor, colon + 1, sizeof(minor));
				cadvisor_label_pair extra[4] = {
					{ "device", dlid->devname },
					{ "major", major },
					{ "minor", minor },
					{ "operation", opname },
				};
				add_cadvisor_metric_labels_uint("container_blkio_device_usage_total", val, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, NULL, extra, 4);
			}
		}

		if (!val)
			continue; // take away all disks, only used

		if (is_seconds)
			add_cadvisor_metric_double(mname, cadvisor_ns_to_seconds(val), cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, NULL);
		else
			add_cadvisor_metric_uint(mname, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, NULL);
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
	carglog(carg, L_TRACE, "cgroup2_get_diskinfo tries open: '%s': %d\n", buf, fd);
	if (!fd) {
		cadvisor_fopen_fail(buf);
		return;
	}

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
			char typebuf[64];
			size_t typelen;
			tmp += strcspn(tmp, "=");
			typelen = (size_t)(tmp - type);
			if (typelen >= sizeof(typebuf))
				typelen = sizeof(typebuf) - 1;
			memcpy(typebuf, type, typelen);
			typebuf[typelen] = '\0';
			tmp += strspn(tmp, "=");

			uint64_t val = strtoull(tmp, NULL, 10);
			int is_max = !strncmp(tmp, "max", 3);
			const char *cost_mname = cadvisor_io_cost_metric_name(typebuf);

			if (!is_max && cost_mname) {
				if (val)
					add_cadvisor_metric_double((char *)cost_mname, cadvisor_usec_to_seconds(val), cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);
			} else if (!is_max) {
				if (!strncmp(typebuf, "rbytes", 6) || !strncmp(typebuf, "wbytes", 6)) {
					char major[16], minor[16];
					char *colon = strchr(disk_id, ':');
					if (colon) {
						size_t majlen = (size_t)(colon - disk_id);
						if (majlen >= sizeof(major))
							majlen = sizeof(major) - 1;
						memcpy(major, disk_id, majlen);
						major[majlen] = '\0';
						strlcpy(minor, colon + 1, sizeof(minor));
						cadvisor_label_pair extra[4] = {
							{ "device", dlid->devname },
							{ "major", major },
							{ "minor", minor },
							{ "operation", !strncmp(typebuf, "rbytes", 6) ? "Read" : "Write" },
						};
						add_cadvisor_metric_labels_uint("container_blkio_device_usage_total", val, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id, extra, 4);
					}
				}
				if (val) {
					if (!strncmp(typebuf, "rbytes", 6) || !strncmp(typebuf, "rbps", 4))
						add_cadvisor_metric_uint(rbps, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);
					else if (!strncmp(typebuf, "wbytes", 6) || !strncmp(typebuf, "wbps", 4))
						add_cadvisor_metric_uint(wbps, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);
					else if (!strncmp(typebuf, "rios", 4) || !strncmp(typebuf, "riops", 5))
						add_cadvisor_metric_uint(riops, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);
					else if (!strncmp(typebuf, "wios", 4) || !strncmp(typebuf, "wiops", 5))
						add_cadvisor_metric_uint(wiops, val, cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, libvirt_id);
				}
			}

			tmp += strcspn(tmp, " \t");
			tmp += strspn(tmp, " \t");
			if (!*tmp)
				break;
		}

	}
	fclose(fd);
}

static void cgroup_blkio_total(alligator_ht* dlid_hash, char *prefix, char *cntid, char *stat, char *mname, int nanoseconds, int keep_zero, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	FILE *fd;
	char buf[PATH_SIZE];
	char readbuf[PATH_SIZE];
	char disk_id[41];
	char *tmp;

	snprintf(buf, PATH_SIZE, "%s/fs/cgroup/blkio/%s/%s/blkio.%s", ac->system_sysfs, prefix, cntid, stat);
	fd = fopen(buf, "r");
	if (!fd) {
		snprintf(buf, PATH_SIZE, "%s/fs/cgroup/blkio/%s/%s/blkio.throttle.%s", ac->system_sysfs, prefix, cntid, stat);
		fd = fopen(buf, "r");
	}
	if (!fd) {
		cadvisor_fopen_fail(buf);
		return;
	}

	while (fgets(readbuf, PATH_SIZE, fd))
	{
		size_t read_size = strcspn(readbuf, " \t\n");
		uint64_t val;
		uint32_t id_hash;
		disk_list_id *dlid;

		if (!read_size)
			continue;
		strlcpy(disk_id, readbuf, read_size + 1);
		tmp = readbuf + read_size;
		tmp += strspn(tmp, " \t");
		if (strncmp(tmp, "Total", 5))
			continue;
		tmp += 5;
		tmp += strspn(tmp, " \t");
		val = strtoull(tmp, NULL, 10);
		if (!val && !keep_zero)
			continue;

		id_hash = tommy_strhash_u32(0, disk_id);
		dlid = alligator_ht_search(dlid_hash, dlid_hash_compare, disk_id, id_hash);
		if (!dlid)
			continue;
		if (nanoseconds)
			add_cadvisor_metric_double(mname, cadvisor_ns_to_seconds(val), cntid, name, image, cad_id, "device", dlid->devname, kubenamespace, kubepod, kubecontainer, NULL);
		else
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
	}
	if (!fd && prefix && *prefix)
	{
		snprintf(fpath, 1000, "%s/fs/cgroup/%s/%s/cgroup.procs", ac->system_sysfs, prefix, container);
		fd = fopen(fpath, "r");
	}
	if (!fd) {
		cadvisor_fopen_fail(fpath);
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
		carglog(ac->cadvisor_carg, L_ERROR, "could not open %s: %s\n", fpath, strerror(errno));
		return;
	}

	while ((fs = getmntent(fp)) != NULL)
	{
		struct statvfs vfsbuf;

		if (strcmp(fs->mnt_dir, "/"))
			continue;
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
		carglog(ac->cadvisor_carg, L_ERROR, "could not open /etc/mtab: %s\n", strerror(errno));
		mlist_free(mlist_hash);
		return NULL;
	}

	while ((fs = getmntent(fp)) != NULL)
	{

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
	long hz = cadvisor_clk_tck();
	snprintf(fpath, 1000, "%s/fs/cgroup/cpu,cpuacct/%s/%s/%s", ac->system_sysfs, prefix, cntid, stat);
	FILE *fd = fopen(fpath, "r");
	if (!fd) {
		cadvisor_fopen_fail(fpath);
		return;
	}

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
			add_cadvisor_metric_double("container_cpu_cfs_throttled_seconds_total", cadvisor_ns_to_seconds(val), cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "system", 6))
		{
			system = val;
			add_cadvisor_metric_double("container_cpu_system_seconds_total", cadvisor_ticks_to_seconds(val, hz), cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
		else if (!strncmp(buf, "user", 4))
		{
			user = val;
			add_cadvisor_metric_double("container_cpu_user_seconds_total", cadvisor_ticks_to_seconds(val, hz), cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
	}
	fclose(fd);

	if (system || user)
	{
		add_cadvisor_metric_double("container_cpu_all_seconds_total", cadvisor_ticks_to_seconds(system + user, hz), cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
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
	snprintf(fpath, 1000, "%s/fs/cgroup/cpu,cpuacct/%s/%s", ac->system_sysfs, cad_id, stat);
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
		add_cadvisor_metric_double(mname, fval, cntid, name, image, cad_id, "cpu", cpuname, kubenamespace, kubepod, kubecontainer, NULL);
		tmp += strcspn(tmp, " \t");
		tmp += strspn(tmp, " \t");
	}

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

	snprintf(fpath, 1000, "%s/fs/cgroup/pids/%s/cgroup.procs", ac->system_sysfs, cad_id);
	FILE *fd = fopen(fpath, "r");
	if (!fd)
	{
		snprintf(fpath, 1000, "%s/fs/cgroup/cpu,cpuacct/%s/cgroup.procs", ac->system_sysfs, cad_id);
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
			carglog(ac->cadvisor_carg, L_ERROR, "fgets: %s\n", strerror(errno));
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
			carglog(ac->cadvisor_carg, L_ERROR, "fgets: %s\n", strerror(errno));
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

	add_cadvisor_metric_uint("container_cpu_schedstat_run_periods_total", run_periods, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_double("container_cpu_schedstat_runqueue_seconds_total", cadvisor_ns_to_seconds(runqueue_time), cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_double("container_cpu_schedstat_run_seconds_total", cadvisor_ns_to_seconds(run_time), cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);

	add_cadvisor_metric_uint("container_tasks_state", running, cntid, name, image, cad_id, "state", "running", kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_uint("container_tasks_state", sleeping, cntid, name, image, cad_id, "state", "sleeping", kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_uint("container_tasks_state", uninterruptible, cntid, name, image, cad_id, "state", "uninterruptible", kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_uint("container_tasks_state", zombie, cntid, name, image, cad_id, "state", "zombie", kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_uint("container_tasks_state", stopped, cntid, name, image, cad_id, "state", "stopped", kubenamespace, kubepod, kubecontainer, NULL);

	add_cadvisor_metric_uint("container_file_descriptors", open_files, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);

	fclose(fd);
}

typedef struct {
	char *cntid;
	char *name;
	char *image;
	char *cad_id;
	char *kubenamespace;
	char *kubepod;
	char *kubecontainer;
} numa_emit_ctx;

static void numa_emit_cb(const char *type, const char *scope, const char *node, uint64_t pages, void *arg)
{
	numa_emit_ctx *ctx = arg;
	cadvisor_label_pair extra[3] = {
		{ "type", type },
		{ "scope", scope },
		{ "node", node },
	};
	add_cadvisor_metric_labels_uint("container_memory_numa_pages", pages, ctx->cntid, ctx->name, ctx->image, ctx->cad_id,
		ctx->kubenamespace, ctx->kubepod, ctx->kubecontainer, NULL, extra, 3);
}

static void cgroup_memory_numa_info(char *cad_id, int cgroup_v2, char *cntid, char *name, char *image, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char fpath[1000];
	char buf[1000];
	FILE *fd;
	numa_emit_ctx ctx = { cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer };

	if (cgroup_v2)
		snprintf(fpath, sizeof(fpath), "%s/fs/cgroup/%s/memory.numa_stat", ac->system_sysfs, cad_id);
	else
		snprintf(fpath, sizeof(fpath), "%s/fs/cgroup/memory/%s/memory.numa_stat", ac->system_sysfs, cad_id);

	fd = fopen(fpath, "r");
	if (!fd)
		return;
	while (fgets(buf, sizeof(buf), fd))
		cadvisor_parse_numa_stat_line(buf, cgroup_v2, numa_emit_cb, &ctx);
	fclose(fd);
}

static void cgroup_memory_migrate_info(char *cad_id, char *cntid, char *name, char *image, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char fpath[1000];
	char buf[64];
	FILE *fd;
	uint64_t val;

	snprintf(fpath, sizeof(fpath), "%s/fs/cgroup/cpuset/%s/cpuset.memory_migrate", ac->system_sysfs, cad_id);
	fd = fopen(fpath, "r");
	if (!fd)
		return;
	if (!fgets(buf, sizeof(buf), fd)) {
		fclose(fd);
		return;
	}
	fclose(fd);
	val = strtoull(buf, NULL, 10);
	add_cadvisor_metric_uint("container_memory_migrate", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
}

static void emit_hugetlb_file(const char *dir, const char *fname, const char *pagesize,
	const char *mname, int is_counter, int skip_max_word,
	char *cntid, char *name, char *image, char *cad_id,
	char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char fpath[1100];
	char buf[128];
	FILE *fd;
	uint64_t val;
	cadvisor_label_pair extra[1];

	snprintf(fpath, sizeof(fpath), "%s/%s", dir, fname);
	fd = fopen(fpath, "r");
	if (!fd)
		return;
	if (!fgets(buf, sizeof(buf), fd)) {
		fclose(fd);
		return;
	}
	fclose(fd);
	if (skip_max_word && !strncmp(buf, "max", 3))
		return;
	val = strtoull(buf, NULL, 10);
	extra[0].name = "pagesize";
	extra[0].value = pagesize;
	if (is_counter)
		add_cadvisor_metric_labels_uint((char *)mname, val, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, NULL, extra, 1);
	else
		add_cadvisor_metric_labels_uint((char *)mname, val, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, NULL, extra, 1);
}

static void emit_hugetlb_events_failcnt(const char *dir, const char *pagesize,
	char *cntid, char *name, char *image, char *cad_id,
	char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char fpath[1100];
	char buf[256];
	FILE *fd;
	snprintf(fpath, sizeof(fpath), "%s/hugetlb.%s.events", dir, pagesize);
	fd = fopen(fpath, "r");
	if (!fd)
		return;
	while (fgets(buf, sizeof(buf), fd)) {
		if (!strncmp(buf, "max", 3)) {
			char *tmp = buf;
			uint64_t val;
			cadvisor_label_pair extra[1];
			tmp += strcspn(tmp, " \t");
			tmp += strspn(tmp, " \t");
			val = strtoull(tmp, NULL, 10);
			extra[0].name = "pagesize";
			extra[0].value = pagesize;
			add_cadvisor_metric_labels_uint("container_hugetlb_failcnt", val, cntid, name, image, cad_id,
				kubenamespace, kubepod, kubecontainer, NULL, extra, 1);
		}
	}
	fclose(fd);
}

static void cgroup_hugetlb_info(char *cad_id, int cgroup_v2, char *cntid, char *name, char *image, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char dir[1000];
	DIR *dp;
	struct dirent *entry;

	if (cgroup_v2)
		snprintf(dir, sizeof(dir), "%s/fs/cgroup/%s", ac->system_sysfs, cad_id);
	else
		snprintf(dir, sizeof(dir), "%s/fs/cgroup/hugetlb/%s", ac->system_sysfs, cad_id);

	dp = opendir(dir);
	if (!dp)
		return;

	while ((entry = readdir(dp))) {
		char pagesize[64];
		const char *rest;
		char *dot;
		if (strncmp(entry->d_name, "hugetlb.", 8))
			continue;
		rest = entry->d_name + 8;
		dot = strchr(rest, '.');
		if (!dot)
			continue;
		if ((size_t)(dot - rest) >= sizeof(pagesize))
			continue;
		memcpy(pagesize, rest, (size_t)(dot - rest));
		pagesize[dot - rest] = '\0';

		if (cgroup_v2) {
			if (!strcmp(dot, ".current"))
				emit_hugetlb_file(dir, entry->d_name, pagesize, "container_hugetlb_usage_bytes", 0, 0,
					cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);
			else if (!strcmp(dot, ".max"))
				emit_hugetlb_file(dir, entry->d_name, pagesize, "container_hugetlb_max_usage_bytes", 0, 1,
					cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);
			else if (!strcmp(dot, ".events"))
				emit_hugetlb_events_failcnt(dir, pagesize, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);
		} else {
			if (!strcmp(dot, ".usage_in_bytes"))
				emit_hugetlb_file(dir, entry->d_name, pagesize, "container_hugetlb_usage_bytes", 0, 0,
					cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);
			else if (!strcmp(dot, ".max_usage_in_bytes"))
				emit_hugetlb_file(dir, entry->d_name, pagesize, "container_hugetlb_max_usage_bytes", 0, 0,
					cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);
			else if (!strcmp(dot, ".failcnt"))
				emit_hugetlb_file(dir, entry->d_name, pagesize, "container_hugetlb_failcnt", 1, 0,
					cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);
		}
	}
	closedir(dp);
}

static void parse_pressure_path(const char *path, const char *resource,
	char *cntid, char *name, char *image, char *cad_id,
	char *kubenamespace, char *kubepod, char *kubecontainer)
{
	FILE *fd;
	char line[256];
	char mname[128];

	fd = fopen(path, "r");
	if (!fd)
		return;
	while (fgets(line, sizeof(line), fd)) {
		char kind[16];
		double a10, a60, a300;
		uint64_t total;
		double seconds;
		if (!cadvisor_parse_pressure_line(line, kind, sizeof(kind), &a10, &a60, &a300, &total))
			continue;
		seconds = cadvisor_usec_to_seconds(total);
		if (!strcmp(kind, "some"))
			snprintf(mname, sizeof(mname), "container_pressure_%s_waiting_seconds_total", resource);
		else if (!strcmp(kind, "full"))
			snprintf(mname, sizeof(mname), "container_pressure_%s_stalled_seconds_total", resource);
		else
			continue;
		add_cadvisor_metric_double(mname, seconds, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	}
	fclose(fd);
}

static void cgroup_pressure_info(char *cad_id, int cgroup_v2, char *cntid, char *name, char *image, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char path[1100];
	const char *resources[] = { "cpu", "memory", "io" };
	size_t i;

	for (i = 0; i < 3; ++i) {
		if (cgroup_v2) {
			snprintf(path, sizeof(path), "%s/fs/cgroup/%s/%s.pressure", ac->system_sysfs, cad_id, resources[i]);
			parse_pressure_path(path, resources[i], cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);
		} else {
			if (!strcmp(resources[i], "cpu"))
				snprintf(path, sizeof(path), "%s/fs/cgroup/cpu,cpuacct/%s/cpu.pressure", ac->system_sysfs, cad_id);
			else if (!strcmp(resources[i], "memory"))
				snprintf(path, sizeof(path), "%s/fs/cgroup/memory/%s/memory.pressure", ac->system_sysfs, cad_id);
			else
				snprintf(path, sizeof(path), "%s/fs/cgroup/blkio/%s/io.pressure", ac->system_sysfs, cad_id);
			parse_pressure_path(path, resources[i], cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);
		}
	}
}

static FILE *open_cgroup_procs(char *cad_id)
{
	char fpath[1000];
	FILE *fd;
	snprintf(fpath, sizeof(fpath), "%s/fs/cgroup/pids/%s/cgroup.procs", ac->system_sysfs, cad_id);
	fd = fopen(fpath, "r");
	if (fd)
		return fd;
	snprintf(fpath, sizeof(fpath), "%s/fs/cgroup/cpu,cpuacct/%s/cgroup.procs", ac->system_sysfs, cad_id);
	fd = fopen(fpath, "r");
	if (fd)
		return fd;
	snprintf(fpath, sizeof(fpath), "%s/fs/cgroup/%s/cgroup.procs", ac->system_sysfs, cad_id);
	return fopen(fpath, "r");
}

static int read_pids_file_u64(char *cad_id, const char *leaf, uint64_t *out, int treat_max_as_zero)
{
	char fpath[1000];
	char buf[128];
	FILE *fd;
	const char *bases[] = {
		"%s/fs/cgroup/pids/%s/%s",
		"%s/fs/cgroup/cpu,cpuacct/%s/%s",
		"%s/fs/cgroup/%s/%s",
	};
	size_t i;

	for (i = 0; i < 3; ++i) {
		snprintf(fpath, sizeof(fpath), bases[i], ac->system_sysfs, cad_id, leaf);
		fd = fopen(fpath, "r");
		if (!fd)
			continue;
		if (!fgets(buf, sizeof(buf), fd)) {
			fclose(fd);
			continue;
		}
		fclose(fd);
		if (treat_max_as_zero && !strncmp(buf, "max", 3))
			*out = 0;
		else
			*out = strtoull(buf, NULL, 10);
		return 1;
	}
	return 0;
}

static void ulimit_label_from_name(const char *src, char *dst, size_t dstsz)
{
	size_t i = 0, j = 0;
	/* skip leading "Max " */
	if (!strncmp(src, "Max ", 4))
		src += 4;
	while (src[i] && j + 1 < dstsz) {
		unsigned char c = (unsigned char)src[i];
		if (isspace(c))
			dst[j++] = '_';
		else
			dst[j++] = (char)tolower(c);
		++i;
	}
	dst[j] = '\0';
}

static void cgroup_process_info(char *cad_id, char *cntid, char *name, char *image, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	FILE *fd;
	char buf[1000];
	char fpath[1100];
	uint64_t processes = 0;
	uint64_t sockets = 0;
	uint64_t threads = 0;
	uint64_t threads_max = 0;
	uint64_t first_pid = 0;
	DIR *dp;
	struct dirent *dent;

	fd = open_cgroup_procs(cad_id);
	if (fd) {
		while (fgets(buf, sizeof(buf), fd)) {
			uint64_t pid = strtoull(buf, NULL, 10);
			if (!pid)
				continue;
			if (!first_pid)
				first_pid = pid;
			++processes;

			snprintf(fpath, sizeof(fpath), "%s/%" PRIu64 "/fd", ac->system_procfs, pid);
			dp = opendir(fpath);
			if (!dp)
				continue;
			while ((dent = readdir(dp))) {
				char linkpath[1200];
				char target[256];
				ssize_t n;
				if (dent->d_name[0] == '.')
					continue;
				snprintf(linkpath, sizeof(linkpath), "%s/%s", fpath, dent->d_name);
				n = readlink(linkpath, target, sizeof(target) - 1);
				if (n < 0)
					continue;
				target[n] = '\0';
				if (!strncmp(target, "socket:", 7))
					++sockets;
			}
			closedir(dp);
		}
		fclose(fd);
	}

	add_cadvisor_metric_uint("container_processes", processes, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_uint("container_sockets", sockets, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);

	if (read_pids_file_u64(cad_id, "pids.current", &threads, 0))
		add_cadvisor_metric_uint("container_threads", threads, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	if (read_pids_file_u64(cad_id, "pids.max", &threads_max, 1))
		add_cadvisor_metric_uint("container_threads_max", threads_max, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);

	if (!first_pid)
		return;

	snprintf(fpath, sizeof(fpath), "%s/%" PRIu64 "/limits", ac->system_procfs, first_pid);
	fd = fopen(fpath, "r");
	if (!fd)
		return;
	/* skip header */
	if (!fgets(buf, sizeof(buf), fd)) {
		fclose(fd);
		return;
	}
	while (fgets(buf, sizeof(buf), fd)) {
		char limname[32];
		char soft[24];
		char ulabel[64];
		int64_t soft_val;
		cadvisor_label_pair extra[1];
		int is_prio;
		size_t n;

		if (strlen(buf) < 45)
			continue;
		memcpy(limname, buf, 25);
		limname[25] = '\0';
		n = 25;
		while (n && limname[n - 1] == ' ')
			limname[--n] = '\0';
		memcpy(soft, buf + 25, 20);
		soft[20] = '\0';
		n = 0;
		while (soft[n] == ' ')
			++n;
		if (n)
			memmove(soft, soft + n, strlen(soft + n) + 1);
		n = strlen(soft);
		while (n && soft[n - 1] == ' ')
			soft[--n] = '\0';

		ulimit_label_from_name(limname, ulabel, sizeof(ulabel));
		is_prio = (!strcmp(ulabel, "nice_priority") || !strcmp(ulabel, "realtime_priority"));
		if (!strcmp(soft, "unlimited"))
			soft_val = is_prio ? 0 : -1;
		else
			soft_val = (int64_t)strtoll(soft, NULL, 10);

		extra[0].name = "ulimit";
		extra[0].value = ulabel;
		add_cadvisor_metric_labels_int("container_ulimits_soft", soft_val, cntid, name, image, cad_id,
			kubenamespace, kubepod, kubecontainer, NULL, extra, 1);
	}
	fclose(fd);
}

typedef struct {
	char *cntid;
	char *name;
	char *image;
	char *cad_id;
	char *kubenamespace;
	char *kubepod;
	char *kubecontainer;
	char *libvirt_id;
} snmp_emit_ctx;

static void snmp_tcp_cb(const char *field, uint64_t val, int negative, void *arg)
{
	snmp_emit_ctx *ctx = arg;
	if (negative)
		add_cadvisor_metric_int("container_network_advance_tcp_stats_total", -(int64_t)val, ctx->cntid, ctx->name, ctx->image, ctx->cad_id,
			"tcp_state", (char *)field, ctx->kubenamespace, ctx->kubepod, ctx->kubecontainer, ctx->libvirt_id);
	else
		add_cadvisor_metric_uint("container_network_advance_tcp_stats_total", val, ctx->cntid, ctx->name, ctx->image, ctx->cad_id,
			"tcp_state", (char *)field, ctx->kubenamespace, ctx->kubepod, ctx->kubecontainer, ctx->libvirt_id);
}

static void cgroup_advance_tcp_stats(uint64_t pid, char *cntid, char *name, char *image, char *cad_id,
	char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	char fpath[1000];
	char header[2048];
	char values[2048];
	FILE *fd;
	snmp_emit_ctx ctx = { cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer, libvirt_id };

	if (!pid)
		return;
	snprintf(fpath, sizeof(fpath), "%s/%" PRIu64 "/net/snmp", ac->system_procfs, pid);
	fd = fopen(fpath, "r");
	if (!fd)
		return;
	while (fgets(header, sizeof(header), fd)) {
		if (strncmp(header, "Tcp:", 4))
			continue;
		if (!fgets(values, sizeof(values), fd))
			break;
		cadvisor_parse_snmp_tcp_pair(header, values, snmp_tcp_cb, &ctx);
		break;
	}
	fclose(fd);
}

static void cgroup_oom_control_info(char *cad_id, char *cntid, char *name, char *image, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char fpath[1000];
	char buf[256];
	FILE *fd;

	snprintf(fpath, sizeof(fpath), "%s/fs/cgroup/memory/%s/memory.oom_control", ac->system_sysfs, cad_id);
	fd = fopen(fpath, "r");
	if (!fd)
		return;
	while (fgets(buf, sizeof(buf), fd)) {
		if (!strncmp(buf, "oom_kill ", 9) || !strncmp(buf, "oom_kill\t", 9)) {
			char *tmp = buf;
			uint64_t val;
			tmp += strcspn(tmp, " \t");
			tmp += strspn(tmp, " \t");
			val = strtoull(tmp, NULL, 10);
			add_cadvisor_metric_uint("container_oom_events_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
	}
	fclose(fd);
}

void cgroup_memory_info(char *prefix, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char fpath[1000];
	char buf[1000];
	char *tmp;
	uint64_t val;
	uint64_t working_set = 0;
	snprintf(fpath, 1000, "%s/fs/cgroup/memory/%s/memory.stat", ac->system_sysfs, cad_id);

	FILE *fd = fopen(fpath, "r");
	carglog(ac->cadvisor_carg, L_TRACE, "cadvisor tries open '%s': %p\n", fpath, fd);
	if (!fd) {
		cadvisor_fopen_fail(fpath);
		return;
	}

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
			carglog(ac->cadvisor_carg, L_TRACE, "container_memory_rss sysfs %s, cntid %s, name %s, image %s, cad_id %s, kubenamespace %s, kubepod %s, kubecontainer %s\n", ac->system_sysfs, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);
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
		else if (!strncmp(buf, "total_active_file", 16))
			add_cadvisor_metric_uint("container_memory_total_active_file_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "total_inactive_file", 18))
			add_cadvisor_metric_uint("container_memory_total_inactive_file_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	}
	fclose(fd);

	snprintf(fpath, 1000, "%s/fs/cgroup/memory/%s/memory.memsw.failcnt", ac->system_sysfs, cad_id);
	fd = fopen(fpath, "r");
	if (fd) {
		if (fgets(buf, 1000, fd)) {
			val = strtoull(buf, NULL, 10);
			add_cadvisor_metric_uint("container_memory_failcnt", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
		fclose(fd);
	}

	snprintf(fpath, 1000, "%s/fs/cgroup/memory/%s/memory.kmem.usage_in_bytes", ac->system_sysfs, cad_id);
	fd = fopen(fpath, "r");
	if (fd) {
		if (fgets(buf, 1000, fd)) {
			val = strtoull(buf, NULL, 10);
			working_set += val;
			add_cadvisor_metric_uint("container_memory_kernel_usage", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
		fclose(fd);
	}
	add_cadvisor_metric_uint("container_memory_working_set_bytes", working_set, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);

	snprintf(fpath, 1000, "%s/fs/cgroup/memory/%s/memory.max_usage_in_bytes", ac->system_sysfs, cad_id);
	fd = fopen(fpath, "r");
	if (fd) {
		if (fgets(buf, 1000, fd)) {
			val = strtoull(buf, NULL, 10);
			add_cadvisor_metric_uint("container_memory_max_usage_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
		fclose(fd);
	}

	snprintf(fpath, 1000, "%s/fs/cgroup/memory/%s/memory.usage_in_bytes", ac->system_sysfs, cad_id);
	fd = fopen(fpath, "r");
	if (fd) {
		if (fgets(buf, 1000, fd)) {
			val = strtoull(buf, NULL, 10);
			add_cadvisor_metric_uint("container_memory_usage_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
		fclose(fd);
	}

	snprintf(fpath, 1000, "%s/fs/cgroup/memory/%s/memory.soft_limit_in_bytes", ac->system_sysfs, cad_id);
	fd = fopen(fpath, "r");
	if (fd) {
		if (fgets(buf, 1000, fd)) {
			val = strtoull(buf, NULL, 10);
			if (val > 1000000000000000)
				val = 0;
			add_cadvisor_metric_uint("container_spec_memory_reservation_limit_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
		fclose(fd);
	}

	cgroup_oom_control_info(cad_id, cntid, name, image, kubenamespace, kubepod, kubecontainer);
	cgroup_memory_numa_info(cad_id, 0, cntid, name, image, kubenamespace, kubepod, kubecontainer);
	cgroup_memory_migrate_info(cad_id, cntid, name, image, kubenamespace, kubepod, kubecontainer);
	cgroup_hugetlb_info(cad_id, 0, cntid, name, image, kubenamespace, kubepod, kubecontainer);
}

void cgroupv2_memory_info(char *prefix, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char fpath[1000];
	char buf[1000];
	char *tmp;
	uint64_t val;
	uint64_t working_set = 0;
	uint64_t memory_usage = 0;
	uint64_t memory_rss = 0;
	snprintf(fpath, 1000, "%s/fs/cgroup/%s/memory.stat", ac->system_sysfs, cad_id);

	FILE *fd = fopen(fpath, "r");
	carglog(ac->cadvisor_carg, L_TRACE, "cadvisor memory v2 tries open '%s': %p\n", fpath, fd);
	if (!fd) {
		cadvisor_fopen_fail(fpath);
		return;
	}

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
			working_set += val;
			memory_usage += val;
			memory_rss += val;
		}
		else if (!strncmp(buf, "inactive_anon", 13)) {
			memory_usage += val;
			memory_rss += val;
			working_set += val;
		}
		else if (!strncmp(buf, "inactive_file", 13)) {
			add_cadvisor_metric_uint("container_memory_cache", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
			add_cadvisor_metric_uint("container_memory_total_inactive_file_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
			memory_usage += val;
		}
		else if (!strncmp(buf, "active_file", 11)) {
			working_set += val;
			memory_usage += val;
			add_cadvisor_metric_uint("container_memory_total_active_file_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
		else if (!strncmp(buf, "pgmajfault", 10))
			add_cadvisor_metric_uint("container_memory_failures_total", val, cntid, name, image, cad_id, "type", "pgmajfault", kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "kernel ", 7))
			add_cadvisor_metric_uint("container_memory_kernel_usage", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "file_dirty", 10))
			add_cadvisor_metric_uint("container_memory_file_dirty_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "file_writeback", 14))
			add_cadvisor_metric_uint("container_memory_file_writeback_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "pgscan ", 7))
			add_cadvisor_metric_uint("container_memory_pgscan_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "pgsteal ", 8))
			add_cadvisor_metric_uint("container_memory_pgsteal_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "workingset_refault_file", 23))
			add_cadvisor_metric_uint("container_memory_workingset_refault_file_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		else if (!strncmp(buf, "workingset_refault_anon", 23))
			add_cadvisor_metric_uint("container_memory_workingset_refault_anon_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	}
	fclose(fd);

	add_cadvisor_metric_uint("container_memory_working_set_bytes", working_set, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_uint("container_memory_usage_bytes", memory_usage, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	add_cadvisor_metric_uint("container_memory_rss", memory_rss, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);

	snprintf(fpath, 1000, "%s/fs/cgroup/%s/memory.swap.max", ac->system_sysfs, cad_id);
	carglog(ac->cadvisor_carg, L_TRACE, "cadvisor memory v2 tries open '%s'\n", fpath);
	fd = fopen(fpath, "r");
	if (fd) {
		if (fgets(buf, 1000, fd) && strncmp(buf, "max", 3)) {
			val = strtoull(buf, NULL, 10);
			add_cadvisor_metric_uint("container_spec_memory_swap_limit_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
		fclose(fd);
	}

	snprintf(fpath, 1000, "%s/fs/cgroup/%s/memory.max", ac->system_sysfs, cad_id);
	carglog(ac->cadvisor_carg, L_TRACE, "cadvisor memory v2 tries open '%s'\n", fpath);
	fd = fopen(fpath, "r");
	if (fd) {
		if (fgets(buf, 1000, fd) && strncmp(buf, "max", 3)) {
			val = strtoull(buf, NULL, 10);
			add_cadvisor_metric_uint("container_spec_memory_limit_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
		fclose(fd);
	}

	snprintf(fpath, 1000, "%s/fs/cgroup/%s/memory.swap.current", ac->system_sysfs, cad_id);
	carglog(ac->cadvisor_carg, L_TRACE, "cadvisor memory v2 tries open '%s'\n", fpath);
	fd = fopen(fpath, "r");
	if (fd) {
		if (fgets(buf, 1000, fd)) {
			val = strtoull(buf, NULL, 10);
			add_cadvisor_metric_uint("container_memory_swap", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer,  NULL);
		}
		fclose(fd);
	}

	snprintf(fpath, 1000, "%s/fs/cgroup/%s/memory.peak", ac->system_sysfs, cad_id);
	fd = fopen(fpath, "r");
	if (fd) {
		if (fgets(buf, 1000, fd) && strncmp(buf, "max", 3)) {
			val = strtoull(buf, NULL, 10);
			add_cadvisor_metric_uint("container_memory_max_usage_bytes", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
		fclose(fd);
	}

	snprintf(fpath, 1000, "%s/fs/cgroup/%s/memory.events", ac->system_sysfs, cad_id);
	carglog(ac->cadvisor_carg, L_TRACE, "cadvisor memory v2 tries open '%s'\n", fpath);
	fd = fopen(fpath, "r");
	if (fd) {
		while(fgets(buf, 1000, fd))
		{
			const char *mname;

			tmp = buf;
			tmp += strcspn(tmp, " \t");
			tmp += strspn(tmp, " \t");
			val = strtoull(tmp, NULL, 10);
			mname = cadvisor_memory_events_metric_name(buf);
			if (mname)
				add_cadvisor_metric_uint((char *)mname, val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
		}
		fclose(fd);
	}

	cgroup_memory_numa_info(cad_id, 1, cntid, name, image, kubenamespace, kubepod, kubecontainer);
	cgroup_hugetlb_info(cad_id, 1, cntid, name, image, kubenamespace, kubepod, kubecontainer);
}

void get_start_time(char *prefix, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	char fpath[1000];
	snprintf(fpath, 1000, "%s/fs/cgroup/cpu,cpuacct/%s", ac->system_sysfs, cad_id);
	uint64_t uptime = get_file_atime(fpath);
	if (!uptime) {
		snprintf(fpath, 1000, "%s/fs/cgroup/%s", ac->system_sysfs, cad_id);
		uptime = get_file_atime(fpath);
	}
	add_cadvisor_metric_uint("container_start_time_seconds", uptime, cntid, name, image, cad_id, 0, 0, kubenamespace, kubepod, kubecontainer, libvirt_id);
}

void cgroup_get_quotas(char *prefix, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer)
{
	char fpath[1000];
	int64_t ival = 0;

	snprintf(fpath, 1000, "%s/fs/cgroup/cpuacct/%s/cpu.shares", ac->system_sysfs, cad_id);
	int rc = get_int_file(fpath, &ival);
    if (rc)
		add_cadvisor_metric_int("container_spec_cpu_shares", ival, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);

	snprintf(fpath, 1000, "%s/fs/cgroup/cpuacct/%s/cpu.cfs_period_us", ac->system_sysfs, cad_id);
	rc = get_int_file(fpath, &ival);
	if (rc)
		add_cadvisor_metric_int("container_spec_cpu_period", ival, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, NULL);
	uint64_t periods = ival;

	snprintf(fpath, 1000, "%s/fs/cgroup/cpuacct/%s/cpu.cfs_quota_us", ac->system_sysfs, cad_id);
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

void cgroup2_cpu_info(char *prefix, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	char fpath[1000];
	char buf[1000];
	char *tmp;
	uint64_t val;
	double dval;
	snprintf(fpath, 1000, "%s/fs/cgroup/%s/cpu.stat", ac->system_sysfs, cad_id);

	FILE *fd = fopen(fpath, "r");
	carglog(ac->cadvisor_carg, L_TRACE, "cadvisor cpu v2 tries open '%s': %p\n", fpath, fd);
	if (!fd) {
		cadvisor_fopen_fail(fpath);
		return;
	}

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
			add_cadvisor_metric_double("container_cpu_cfs_periods_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
		else if (!strncmp(buf, "nr_throttled", 12))
			add_cadvisor_metric_double("container_cpu_cfs_throttled_periods_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
		else if (!strncmp(buf, "throttled_usec", 14))
			add_cadvisor_metric_double("container_cpu_cfs_throttled_seconds_total", dval * 1.0 / 1000000, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
		else if (!strncmp(buf, "nr_bursts", 9))
			add_cadvisor_metric_uint("container_cpu_cfs_burst_periods_total", val, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
		else if (!strncmp(buf, "burst_usec", 10))
			add_cadvisor_metric_double("container_cpu_cfs_burst_seconds_total", dval * 1.0 / 1000000, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
	}
	fclose(fd);

	snprintf(fpath, 1000, "%s/fs/cgroup/%s/cpu.max", ac->system_sysfs, cad_id);
	carglog(ac->cadvisor_carg, L_TRACE, "cadvisor cpu v2 tries open '%s': %p\n", fpath, fd);
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

	carglog(ac->cadvisor_carg, L_TRACE, "cadvisor cpu max %"PRIu64", period %"PRIu64"\n", max, period);
	add_cadvisor_metric_uint("container_spec_cpu_period", period, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
	add_cadvisor_metric_uint("container_spec_cpu_quota", max, cntid, name, image, cad_id, NULL, NULL, kubenamespace, kubepod, kubecontainer, libvirt_id);
}

void cgroup2_libvirt_cpu_shares(char *prefix, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id) {
	struct dirent *libvirt_cpu;

	char libvirtdir[256];
	snprintf(libvirtdir, 255, "%s/fs/cgroup/%s/libvirt", ac->system_sysfs, cad_id);
	DIR *rd = opendir(libvirtdir);
	carglog(ac->cadvisor_carg, L_TRACE, "cadvisor cpu shares v2 tries open '%s': %p\n", libvirtdir, rd);
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
	char pidsdir[1000];

	carglog(ac->cadvisor_carg, L_TRACE, "cadvisor_network_scrape sysfs %s, cntid %s, name %s, image %s, cad_id %s, kubenamespace %s, kubepod %s, kubecontainer %s\n", sysfs, cntid, name, image, cad_id, kubenamespace, kubepod, kubecontainer);

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

	{
		cadvisor_net_emit_ctx emit_ctx = {
			.cntid = cntid,
			.name = name,
			.image = image,
			.cad_id = cad_id,
			.kubenamespace = kubenamespace,
			.kubepod = kubepod,
			.kubecontainer = kubecontainer,
			.libvirt_id = libvirt_id,
		};

		network_netlink_foreach(cadvisor_netlink_emit_cb, &emit_ctx, 0, ac->cadvisor_carg, "/var/lib/alligator/nsmount");
	}

	int rc = unshare(CLONE_NEWNET);
	if (rc)
		carglog(ac->cadvisor_carg, L_ERROR, "unshare %s failed: %d\n", name, rc);

	mount_ns_by_pid("1");
}

void cadvisor_scrape(char *ifname, char *cgroupPath, char *slice, char *cntid, char *name, char *image, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id)
{
	int cgroupver = 0;
	char cgroupfsdir[255];
	snprintf(cgroupfsdir, 255, "%s/fs/cgroup/", ac->system_sysfs);
	struct statfs cgroupdirstat;
	statfs(cgroupfsdir, &cgroupdirstat);
	if (cgroupdirstat.f_type == CGROUP_SUPER_MAGIC)
		cgroupver = 1;
	else if (cgroupdirstat.f_type == TMPFS_MAGIC)
		cgroupver = 1;
	else if (cgroupdirstat.f_type == CGROUP2_SUPER_MAGIC)
		cgroupver = 2;

	carglog(ac->cadvisor_carg, L_TRACE, "cadvisor_scrape search id in slice %s id: %s with name: %s and version %d\n", slice, cntid, name, cgroupver);
	if (!ac->cadvisor_tcpudpbuf)
		ac->cadvisor_tcpudpbuf = malloc(TCPUDP_NET_LENREAD);


	char cad_id[255];
	if (!cgroupPath) {
		if (*slice != 0)
			snprintf(cad_id, 255, "/%s/%s", slice, cntid);
		else
			snprintf(cad_id, 255, "/%s", cntid);
		cgroupPath = cad_id;
	}

	cadvisor_network_scrape(ac->system_sysfs, cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer, libvirt_id);
	if (ifname)
	{
		network_if_stats st;

		if (network_if_stats_netlink(ifname, &st) == 0)
			cgroup_emit_netinfo_stats(&st, cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer, libvirt_id);
	}



	alligator_ht* mlist_hash = get_mountpoint_list();
	alligator_ht* dlid_hash = get_disk_ids_names(mlist_hash);
	uint64_t pid = cgroup_get_pid(slice, cntid);
	if (!pid && cgroupPath && *cgroupPath)
	{
		char fpath[1000];
		char buf[64];
		FILE *pfd;
		snprintf(fpath, sizeof(fpath), "%s/fs/cgroup/%s/cgroup.procs", ac->system_sysfs, cgroupPath);
		pfd = fopen(fpath, "r");
		if (pfd && fgets(buf, sizeof(buf), pfd))
			pid = strtoull(buf, NULL, 10);
		if (pfd)
			fclose(pfd);
	}

	if (dlid_hash)
		cadvisor_emit_rootfs_diskstats(cadvisor_rootfs_disk(pid, dlid_hash), cgroupver, cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer, libvirt_id);

	if (mlist_hash)
	{
		cgroup_fd_disk_part_info(slice, pid, cntid, mlist_hash, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer);
		mlist_free(mlist_hash);
	}

	if (dlid_hash)
	{
		if (cgroupver == 1) {
			cgroup_get_diskinfo(dlid_hash, slice, cntid, "io_merged", "container_fs_%s_merged_total", name, image, cgroupPath, "writes", "reads", kubenamespace, kubepod, kubecontainer);
			cgroup_get_diskinfo(dlid_hash, slice, cntid, "sectors", "container_fs_sector_%s_total", name, image, cgroupPath, "writes", "reads", kubenamespace, kubepod, kubecontainer);
			cgroup_get_diskinfo(dlid_hash, slice, cntid, "io_serviced", "container_fs_%s_total", name, image, cgroupPath, "writes", "reads", kubenamespace, kubepod, kubecontainer);
			cgroup_get_diskinfo(dlid_hash, slice, cntid, "io_service_bytes", "container_fs_%s_bytes_total", name, image, cgroupPath, "writes", "reads", kubenamespace, kubepod, kubecontainer);
			cgroup_get_diskinfo(dlid_hash, slice, cntid, "io_service_time", "container_fs_%s_seconds_total", name, image, cgroupPath, "write", "read", kubenamespace, kubepod, kubecontainer);
			cgroup_blkio_total(dlid_hash, slice, cntid, "io_service_time", "container_fs_io_time_seconds_total", 1, 0, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer);
			cgroup_blkio_total(dlid_hash, slice, cntid, "io_queued", "container_fs_io_current", 0, 1, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer);
		}

		cgroup_get_tcpudpinfo(ac->cadvisor_tcpudpbuf, pid, "tcp", cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer, libvirt_id);
		cgroup_get_tcpudpinfo(ac->cadvisor_tcpudpbuf, pid, "udp", cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer, libvirt_id);
		cgroup_get_tcpudpinfo(ac->cadvisor_tcpudpbuf, pid, "tcp6", cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer, libvirt_id);
		cgroup_get_tcpudpinfo(ac->cadvisor_tcpudpbuf, pid, "udp6", cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer, libvirt_id);
		cgroup_advance_tcp_stats(pid, cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer, libvirt_id);

		cgroup2_get_diskinfo(ac->cadvisor_carg, dlid_hash, slice, cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer, libvirt_id, "io.stat", "container_fs_reads_bytes_total", "container_fs_writes_bytes_total", "container_fs_reads_total", "container_fs_writes_total");
		cgroup2_get_diskinfo(ac->cadvisor_carg, dlid_hash, slice, cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer, libvirt_id, "io.max", "container_spec_fs_reads_rate_limit_bytes", "container_spec_fs_writes_rate_limit_bytes", "container_spec_fs_reads_rate_limit_total", "container_spec_fs_writes_rate_limit_total");

		dlid_free(dlid_hash);
	}

	cgroup_get_cpuinfo(slice, cntid, "cpu.stat", name, image, cgroupPath, kubenamespace, kubepod, kubecontainer);
	cgroup_get_cpuinfo(slice, cntid, "cpuacct.stat", name, image, cgroupPath, kubenamespace, kubepod, kubecontainer);
	cgroup2_cpu_info(slice, cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer, libvirt_id);
	cgroup2_libvirt_cpu_shares(slice, cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer, libvirt_id);
	cgroup_get_cpuacctinfo(slice, cntid, "cpuacct.usage_percpu", "container_cpu_usage_seconds_total", name, image, cgroupPath, kubenamespace, kubepod, kubecontainer);
	cgroup_get_quotas(slice, cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer);
	cgroup_cpu_schedstats_info(slice, cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer);
	cgroup_process_info(cgroupPath, cntid, name, image, kubenamespace, kubepod, kubecontainer);
	cgroup_pressure_info(cgroupPath, cgroupver == 2, cntid, name, image, kubenamespace, kubepod, kubecontainer);

	cgroup_memory_info(slice, cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer);
	if (!libvirt_id)
		cgroupv2_memory_info(slice, cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer);
	get_start_time(slice, cntid, name, image, cgroupPath, kubenamespace, kubepod, kubecontainer, libvirt_id);
	cadvisor_hw_scrape(cgroupPath, cntid, name, image, kubenamespace, kubepod, kubecontainer, libvirt_id);

	r_time now = setrtime();
	uint64_t last_seen = now.sec;
	add_cadvisor_metric_uint("container_last_seen", last_seen, cntid, name, image, cgroupPath, 0, 0, kubenamespace, kubepod, kubecontainer, libvirt_id);
}
#else
#include "cadvisor/metrics.h"

void cadvisor_register_metric_families(context_arg *carg)
{
	(void)carg;
}
#endif
