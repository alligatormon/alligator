#ifdef __APPLE__
#include <inttypes.h>
#include <ctype.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <sys/proc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <libproc.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <ifaddrs.h>

#include "metric/labels.h"
#include "main.h"
#include "common/rtime.h"
#include "system/common.h"
#include "system/macosx/parsers.h"
#include "system/macosx/sysctl.h"

extern aconf *ac;

typedef struct mac_cpu_ticks {
	uint64_t user;
	uint64_t nice;
	uint64_t system;
	uint64_t idle;
} mac_cpu_ticks;

static void get_cpu_avg(void)
{
	double result = ac->system_cpuavg_sum / ac->system_cpuavg_period;
	metric_add_auto("cpu_usage_average_percent", &result, DATATYPE_DOUBLE, ac->system_carg);
}

static void cpu_avg_push(double now)
{
	if (now > 100)
		return;

	double old = ac->system_avg_metrics[ac->system_cpuavg_ptr];
	ac->system_avg_metrics[ac->system_cpuavg_ptr] = now;
	ac->system_cpuavg_sum = (ac->system_cpuavg_sum - old) + now;

	++ac->system_cpuavg_ptr;
	if (ac->system_cpuavg_ptr >= ac->system_cpuavg_period)
		ac->system_cpuavg_ptr = 0;
}

static double pct_delta(uint64_t delta, uint64_t total)
{
	if (!total)
		return 0.0;
	double pct = (double)delta * 100.0 / (double)total;
	return pct < 0.0 ? 0.0 : pct;
}

static long cpu_tick_hz(void)
{
	static long hz;
	struct clockinfo clock;
	size_t size;
	int mib[2];

	if (hz > 0)
		return hz;

	mib[0] = CTL_KERN;
	mib[1] = KERN_CLOCKRATE;
	size = sizeof(clock);
	if (sysctl(mib, 2, &clock, &size, NULL, 0) == 0) {
		hz = clock.stathz > 0 ? clock.stathz : clock.hz;
		if (hz > 0)
			return hz;
	}

	hz = sysconf(_SC_CLK_TCK);
	if (hz <= 0)
		hz = 100;
	return hz;
}

static double ticks_to_seconds(uint64_t ticks)
{
	return (double)ticks / (double)cpu_tick_hz();
}

static int read_cpu_ticks(mac_cpu_ticks *agg, mac_cpu_ticks **cores, natural_t *processor_count)
{
	processor_cpu_load_info_t cpu_load;
	mach_msg_type_number_t msg_count;
	kern_return_t kr;
	natural_t count;
	natural_t i;

	kr = host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &count,
	    (processor_info_array_t *)&cpu_load, &msg_count);
	if (kr != KERN_SUCCESS)
		return -1;

	if (!*cores)
		*cores = calloc(count, sizeof(mac_cpu_ticks));
	if (!*cores) {
		vm_deallocate(mach_task_self(), (vm_address_t)cpu_load, msg_count * sizeof(integer_t));
		return -1;
	}

	memset(agg, 0, sizeof(*agg));
	for (i = 0; i < count; i++) {
		(*cores)[i].user = cpu_load[i].cpu_ticks[CPU_STATE_USER];
		(*cores)[i].nice = cpu_load[i].cpu_ticks[CPU_STATE_NICE];
		(*cores)[i].system = cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM];
		(*cores)[i].idle = cpu_load[i].cpu_ticks[CPU_STATE_IDLE];

		agg->user += (*cores)[i].user;
		agg->nice += (*cores)[i].nice;
		agg->system += (*cores)[i].system;
		agg->idle += (*cores)[i].idle;
	}

	vm_deallocate(mach_task_self(), (vm_address_t)cpu_load, msg_count * sizeof(integer_t));
	*processor_count = count;
	return 0;
}

static void cpu_core_interval_push(system_cpu_cores_stats *sccs, const char *cpuname,
	const mac_cpu_ticks *now)
{
	uint64_t t_total = now->user + now->nice + now->system + now->idle;
	uint64_t dt_total = t_total - sccs->total;

	if (!dt_total)
		return;

	{
		uint64_t d_user = now->user - sccs->user;
		uint64_t d_nice = now->nice - sccs->nice;
		uint64_t d_system = now->system - sccs->system;
		uint64_t d_idle = now->idle - sccs->idle;
		double user = pct_delta(d_user, dt_total);
		double nice = pct_delta(d_nice, dt_total);
		double system = pct_delta(d_system, dt_total);
		double idle = pct_delta(d_idle, dt_total);

		metric_add_labels2("cpu_usage_core", &user, DATATYPE_DOUBLE, ac->system_carg, "type", "user", "cpu", (char *)cpuname);
		metric_add_labels2("cpu_usage_core", &nice, DATATYPE_DOUBLE, ac->system_carg, "type", "nice", "cpu", (char *)cpuname);
		metric_add_labels2("cpu_usage_core", &system, DATATYPE_DOUBLE, ac->system_carg, "type", "system", "cpu", (char *)cpuname);
		metric_add_labels2("cpu_usage_core", &idle, DATATYPE_DOUBLE, ac->system_carg, "type", "idle", "cpu", (char *)cpuname);
	}

	sccs->total = t_total;
	sccs->user = now->user;
	sccs->nice = now->nice;
	sccs->system = now->system;
	sccs->idle = now->idle;
}

static void emit_cpu_counters(const mac_cpu_ticks *ticks, const char *cpuname, int per_core)
{
	double user = ticks_to_seconds(ticks->user);
	double nice = ticks_to_seconds(ticks->nice);
	double system = ticks_to_seconds(ticks->system);
	double idle = ticks_to_seconds(ticks->idle);

	if (per_core) {
		metric_add_labels2("cpu_usage_core_time", &user, DATATYPE_DOUBLE, ac->system_carg, "cpu", (char *)cpuname, "type", "user");
		metric_add_labels2("cpu_usage_core_time", &nice, DATATYPE_DOUBLE, ac->system_carg, "cpu", (char *)cpuname, "type", "nice");
		metric_add_labels2("cpu_usage_core_time", &system, DATATYPE_DOUBLE, ac->system_carg, "cpu", (char *)cpuname, "type", "system");
		metric_add_labels2("cpu_usage_core_time", &idle, DATATYPE_DOUBLE, ac->system_carg, "cpu", (char *)cpuname, "type", "idle");
	} else {
		metric_add_labels("cpu_usage_time", &user, DATATYPE_DOUBLE, ac->system_carg, "type", "user");
		metric_add_labels("cpu_usage_time", &nice, DATATYPE_DOUBLE, ac->system_carg, "type", "nice");
		metric_add_labels("cpu_usage_time", &system, DATATYPE_DOUBLE, ac->system_carg, "type", "system");
		metric_add_labels("cpu_usage_time", &idle, DATATYPE_DOUBLE, ac->system_carg, "type", "idle");
	}
}

static void get_cpu_frequency(void)
{
	uint64_t freq = 0;
	size_t size = sizeof(freq);
	int ncpu = 0;
	size_t nlen = sizeof(ncpu);
	char cpuname[16];
	int i;

	if (sysctlbyname("hw.ncpu", &ncpu, &nlen, NULL, 0) != 0 || ncpu < 1)
		return;

	if (sysctlbyname("hw.cpufrequency_max", &freq, &size, NULL, 0) != 0 || !freq) {
		size = sizeof(freq);
		if (sysctlbyname("hw.perflevel0.frequency_target", &freq, &size, NULL, 0) != 0 || !freq)
			return;
	}

	for (i = 0; i < ncpu; i++) {
		double hz = (double)freq;
		snprintf(cpuname, sizeof(cpuname), "%d", i);
		metric_add_labels("cpu_current_frequency_hertz", &hz, DATATYPE_DOUBLE, ac->system_carg, "core", cpuname);
	}
}

void get_cpu(void)
{
	mac_cpu_ticks agg;
	mac_cpu_ticks *cores = NULL;
	natural_t processor_count = 0;
	system_cpu_cores_stats *hw;
	r_time ts_start = setrtime();
	r_time ts_end;
	double diff_time;
	uint64_t sec;
	char cpuname[20];
	natural_t i;

	if (read_cpu_ticks(&agg, &cores, &processor_count) != 0)
		return;

	if (!ac->scs->cores && processor_count > 0)
		ac->scs->cores = calloc(processor_count, sizeof(system_cpu_cores_stats));

	emit_cpu_counters(&agg, NULL, 0);

	for (i = 0; i < processor_count; i++) {
		snprintf(cpuname, sizeof(cpuname), "%u", i);
		emit_cpu_counters(&cores[i], cpuname, 1);
		if (ac->scs->cores && i < processor_count)
			cpu_core_interval_push(&ac->scs->cores[i], cpuname, &cores[i]);
	}

	hw = &ac->scs->hw;
	{
		uint64_t t_total = agg.user + agg.nice + agg.system + agg.idle;
		uint64_t dt_total = t_total - hw->total;

		if (dt_total && ac->system_cpuavg) {
			uint64_t d_user = agg.user - hw->user;
			uint64_t d_system = agg.system - hw->system;
			cpu_avg_push(pct_delta(d_user + d_system, dt_total));
		}
		hw->total = t_total;
		hw->user = agg.user;
		hw->nice = agg.nice;
		hw->system = agg.system;
		hw->idle = agg.idle;
	}

	free(cores);

	ts_end = setrtime();
	diff_time = getrtime_ns(ac->last_time_cpu, ts_start) / 1000000000.0;
	ac->last_time_cpu = ts_start;
	metric_add_auto("cpu_usage_calc_delta_seconds", &diff_time, DATATYPE_DOUBLE, ac->system_carg);

	sec = ts_end.sec;
	metric_add_auto("time_now", &sec, DATATYPE_UINT, ac->system_carg);
}

static int skip_mount(const char *path)
{
	return !strncmp(path, "/dev", 4)
	    || !strncmp(path, "/private/var/vm", 15)
	    || !strncmp(path, "/System/Volumes/Hardware", 24);
}

void get_disk(void)
{
	struct statfs *mounts;
	int num_mounts = getmntinfo(&mounts, MNT_WAIT);
	int i;

	metric_add_auto("mounts_num", &num_mounts, DATATYPE_INT, ac->system_carg);
	for (i = 0; i < num_mounts; i++) {
		struct statfs stats;
		int64_t avail, total, used;
		double pused, pfree;
		uint64_t inodes_used;
		double iused, ifree;
		int64_t one = 1;

		if (skip_mount(mounts[i].f_mntonname))
			continue;
		if (statfs(mounts[i].f_mntonname, &stats))
			continue;

		avail = (int64_t)stats.f_bsize * stats.f_bavail;
		total = (int64_t)stats.f_bsize * stats.f_blocks;
		if (total <= 0)
			continue;
		used = total - avail;
		pused = (double)used * 100.0 / (double)total;
		pfree = 100.0 - pused;

		metric_add_labels2("disk_usage", &avail, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "free");
		metric_add_labels2("disk_usage", &total, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "total");
		metric_add_labels2("disk_usage", &used, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "used");
		metric_add_labels2("disk_usage_percent", &pused, DATATYPE_DOUBLE, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "used");
		metric_add_labels2("disk_usage_percent", &pfree, DATATYPE_DOUBLE, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "free");

		if (stats.f_files > 0) {
			inodes_used = stats.f_files - stats.f_ffree;
			iused = inodes_used * 100.0 / stats.f_files;
			ifree = 100.0 - iused;
			metric_add_labels2("disk_inodes", &stats.f_ffree, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "free");
			metric_add_labels2("disk_inodes", &inodes_used, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "used");
			metric_add_labels2("disk_inodes", &stats.f_files, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "total");
			metric_add_labels2("disk_inodes_percent", &iused, DATATYPE_DOUBLE, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "used");
			metric_add_labels2("disk_inodes_percent", &ifree, DATATYPE_DOUBLE, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "free");
		}

		metric_add_labels2("disk_filesystem", &one, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "fs", mounts[i].f_fstypename);
	}
}

void get_mem(void)
{
	int64_t physical_memory = 0;
	size_t length = sizeof(physical_memory);
	int mib[2] = {CTL_HW, HW_MEMSIZE};
	vm_size_t page_size;
	mach_port_t mach_port;
	mach_msg_type_number_t count;
	vm_statistics64_data_t vm_stats;
	int64_t free_memory, used_memory, active_memory, inactive_memory, wire_memory;
	int64_t avail_memory, compressed_memory, purgeable_memory, speculative_memory;
	int64_t external_memory, internal_memory;
	int64_t vm_faults;
	int64_t num_cpu = 0;
	size_t cpu_len = sizeof(num_cpu);
	double load1, load5, load15;
	struct loadavg load;
	size_t load_len = sizeof(load);

	sysctl(mib, 2, &physical_memory, &length, NULL, 0);
	sysctlbyname("hw.ncpu", &num_cpu, &cpu_len, NULL, 0);

	mach_port = mach_host_self();
	count = HOST_VM_INFO64_COUNT;
	if (host_page_size(mach_port, &page_size) != KERN_SUCCESS
	    || host_statistics64(mach_port, HOST_VM_INFO64, (host_info64_t)&vm_stats, &count) != KERN_SUCCESS)
		return;

	free_memory = (int64_t)vm_stats.free_count * (int64_t)page_size;
	active_memory = (int64_t)vm_stats.active_count * (int64_t)page_size;
	inactive_memory = (int64_t)vm_stats.inactive_count * (int64_t)page_size;
	wire_memory = (int64_t)vm_stats.wire_count * (int64_t)page_size;
	compressed_memory = (int64_t)vm_stats.compressor_page_count * (int64_t)page_size;
	purgeable_memory = (int64_t)vm_stats.purgeable_count * (int64_t)page_size;
	speculative_memory = (int64_t)vm_stats.speculative_count * (int64_t)page_size;
	external_memory = (int64_t)vm_stats.external_page_count * (int64_t)page_size;
	internal_memory = (int64_t)vm_stats.internal_page_count * (int64_t)page_size;
	avail_memory = free_memory + inactive_memory + speculative_memory;
	used_memory = active_memory + wire_memory + compressed_memory;
	vm_faults = (int64_t)vm_stats.faults;

	metric_add_labels("memory_usage", &physical_memory, DATATYPE_INT, ac->system_carg, "type", "total");
	metric_add_labels("memory_usage", &free_memory, DATATYPE_INT, ac->system_carg, "type", "free");
	metric_add_labels("memory_usage", &used_memory, DATATYPE_INT, ac->system_carg, "type", "used");
	metric_add_labels("memory_usage", &active_memory, DATATYPE_INT, ac->system_carg, "type", "active");
	metric_add_labels("memory_usage", &inactive_memory, DATATYPE_INT, ac->system_carg, "type", "inactive");
	metric_add_labels("memory_usage", &wire_memory, DATATYPE_INT, ac->system_carg, "type", "wire");
	metric_add_labels("memory_usage", &avail_memory, DATATYPE_INT, ac->system_carg, "type", "available");
	metric_add_labels("memory_usage", &compressed_memory, DATATYPE_INT, ac->system_carg, "type", "compressed");
	metric_add_labels("memory_usage", &purgeable_memory, DATATYPE_INT, ac->system_carg, "type", "purgeable");
	metric_add_labels("memory_usage", &speculative_memory, DATATYPE_INT, ac->system_carg, "type", "speculative");
	metric_add_labels("memory_usage", &external_memory, DATATYPE_INT, ac->system_carg, "type", "external");
	metric_add_labels("memory_usage", &internal_memory, DATATYPE_INT, ac->system_carg, "type", "internal");

	if (physical_memory > 0) {
		double percentused = (double)used_memory * 100.0 / (double)physical_memory;
		double percentfree = 100.0 - percentused;
		metric_add_labels("memory_usage_percent", &percentused, DATATYPE_DOUBLE, ac->system_carg, "type", "used");
		metric_add_labels("memory_usage_percent", &percentfree, DATATYPE_DOUBLE, ac->system_carg, "type", "free");
	}

	{
		int64_t pageins = (int64_t)vm_stats.pageins;
		int64_t pageouts = (int64_t)vm_stats.pageouts;
		int64_t swapins = (int64_t)vm_stats.swapins;
		int64_t swapouts = (int64_t)vm_stats.swapouts;

		metric_add_labels("memory_stat", &pageins, DATATYPE_INT, ac->system_carg, "type", "pgpgin");
		metric_add_labels("memory_stat", &pageouts, DATATYPE_INT, ac->system_carg, "type", "pgpgout");
		metric_add_labels("memory_stat", &swapins, DATATYPE_INT, ac->system_carg, "type", "pgswpin");
		metric_add_labels("memory_stat", &swapouts, DATATYPE_INT, ac->system_carg, "type", "pgswpout");
	}
	metric_add_auto("vm_faults", &vm_faults, DATATYPE_INT, ac->system_carg);

	if (sysctlbyname("vm.loadavg", &load, &load_len, NULL, 0) == 0) {
		load1 = (double)load.ldavg[0] / (double)load.fscale;
		load5 = (double)load.ldavg[1] / (double)load.fscale;
		load15 = (double)load.ldavg[2] / (double)load.fscale;
		metric_add_labels("load_average", &load1, DATATYPE_DOUBLE, ac->system_carg, "type", "load1");
		metric_add_labels("load_average", &load5, DATATYPE_DOUBLE, ac->system_carg, "type", "load5");
		metric_add_labels("load_average", &load15, DATATYPE_DOUBLE, ac->system_carg, "type", "load15");
	}

	metric_add_auto("cores_num", &num_cpu, DATATYPE_INT, ac->system_carg);
}

static void get_sysctl_stat_u64(const char *ctlname, const char *mapping)
{
	uint64_t val;
	size_t size = sizeof(val);

	if (sysctlbyname(ctlname, &val, &size, NULL, 0) != 0)
		return;
	metric_add_auto((char *)mapping, &val, DATATYPE_UINT, ac->system_carg);
}

static void get_vmstat(void)
{
	get_sysctl_stat_u64("kern.num_files", "open_files");
	get_sysctl_stat_u64("kern.maxfiles", "max_files");
	get_sysctl_stat_u64("kern.maxproc", "processes_max");
}

void get_iface_statistics(void)
{
	struct ifaddrs *ifap, *ifa;

	if (getifaddrs(&ifap) != 0)
		return;

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		struct if_data *ifd;
		const char *state;
		int64_t marker;
		int64_t ipackets, opackets, ibytes, obytes;
		int64_t ierrors, oerrors, iqdrops, oqdrops;
		int64_t imcasts, omcasts, noproto, collisions;

		if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK)
			continue;
		if (!ifa->ifa_data)
			continue;

		ifd = ifa->ifa_data;
		state = (ifa->ifa_flags & IFF_UP) ? "up" : "down";
		marker = 1;
		metric_add_labels2("link_status", &marker, DATATYPE_INT, ac->system_carg, "ifname", (char *)ifa->ifa_name, "state", (char *)state);

		{
			int64_t if_up = (ifa->ifa_flags & IFF_UP) ? 1 : 0;
			metric_add_labels("if_up", &if_up, DATATYPE_INT, ac->system_carg, "ifname", ifa->ifa_name);
		}

		ipackets = ifd->ifi_ipackets;
		opackets = ifd->ifi_opackets;
		ibytes = ifd->ifi_ibytes;
		obytes = ifd->ifi_obytes;
		ierrors = ifd->ifi_ierrors;
		oerrors = ifd->ifi_oerrors;
		iqdrops = ifd->ifi_iqdrops;
		oqdrops = 0;
		imcasts = ifd->ifi_imcasts;
		omcasts = ifd->ifi_omcasts;
		noproto = ifd->ifi_noproto;
		collisions = ifd->ifi_collisions;

		metric_add_labels2("if_stat", &ibytes, DATATYPE_INT, ac->system_carg, "ifname", (char *)ifa->ifa_name, "type", "received_bytes");
		metric_add_labels2("if_stat", &obytes, DATATYPE_INT, ac->system_carg, "ifname", (char *)ifa->ifa_name, "type", "transmit_bytes");
		metric_add_labels2("if_stat", &ipackets, DATATYPE_INT, ac->system_carg, "ifname", (char *)ifa->ifa_name, "type", "received_packets");
		metric_add_labels2("if_stat", &opackets, DATATYPE_INT, ac->system_carg, "ifname", (char *)ifa->ifa_name, "type", "transmit_packets");
		metric_add_labels2("if_stat", &ierrors, DATATYPE_INT, ac->system_carg, "ifname", (char *)ifa->ifa_name, "type", "received_err");
		metric_add_labels2("if_stat", &oerrors, DATATYPE_INT, ac->system_carg, "ifname", (char *)ifa->ifa_name, "type", "transmit_err");
		metric_add_labels2("if_stat", &iqdrops, DATATYPE_INT, ac->system_carg, "ifname", (char *)ifa->ifa_name, "type", "received_drop");
		metric_add_labels2("if_stat", &oqdrops, DATATYPE_INT, ac->system_carg, "ifname", (char *)ifa->ifa_name, "type", "transmit_drop");
		metric_add_labels2("if_stat", &imcasts, DATATYPE_INT, ac->system_carg, "ifname", (char *)ifa->ifa_name, "type", "received_multicast");
		metric_add_labels2("if_stat", &omcasts, DATATYPE_INT, ac->system_carg, "ifname", (char *)ifa->ifa_name, "type", "transmit_multicast");
		metric_add_labels2("if_stat", &collisions, DATATYPE_INT, ac->system_carg, "ifname", (char *)ifa->ifa_name, "type", "transmit_colls");
		metric_add_labels2("if_stat", &noproto, DATATYPE_INT, ac->system_carg, "ifname", (char *)ifa->ifa_name, "type", "noproto");

		if (ifd->ifi_baudrate > 0) {
			int64_t speed = (int64_t)ifd->ifi_baudrate;
			metric_add_labels("if_speed", &speed, DATATYPE_INT, ac->system_carg, "ifname", ifa->ifa_name);
		}
	}
	freeifaddrs(ifap);
}

static void emit_uptime(void)
{
	struct timeval boottime;
	size_t len = sizeof(boottime);
	time_t now = time(NULL);
	uint64_t uptime;

	if (sysctlbyname("kern.boottime", &boottime, &len, NULL, 0) != 0)
		return;
	if (now <= boottime.tv_sec)
		return;
	uptime = (uint64_t)(now - boottime.tv_sec);
	metric_add_auto("system_uptime_seconds", &uptime, DATATYPE_UINT, ac->system_carg);
}

void get_system_metrics(void)
{
	if (ac->system_base) {
		int8_t platform;

		get_mem();
		get_vmstat();
		emit_uptime();
		ipaddr_info();
		hw_cpu_info();
		get_utsname();
		platform = get_platform(1);
		get_kernel_version(platform);
		get_distribution_name();
		get_memory_usage_hw();
	}
	if (ac->system_base && !ac->system_process)
		get_proc_info(1);
	if (ac->system_network) {
		get_iface_statistics();
		get_network_statistics();
		check_sockets_macos();
	}
	if (ac->system_disk) {
		get_disk();
		disk_io_stats();
	}
	if (ac->system_process)
		get_proc_info(0);
	if (ac->system_firewall)
		bsd_firewall_collect();
	if (ac->system_cpuavg)
		get_cpu_avg();

	if (ac->system_services || ac->system_services_process)
		get_services();

	get_pidfile_stats();
	get_userprocess_stats();
	sysctl_run(ac->system_sysctl);
}

void system_fast_scrape(void)
{
	if (ac->system_base) {
		get_cpu();
		get_cpu_frequency();
	}
}

void system_slow_scrape(void)
{
	if (ac->system_base)
		get_smbios();
	if (ac->system_disk)
		disks_info();
}

void system_free(void)
{
	if (ac->scs) {
		if (ac->scs->cores)
			free(ac->scs->cores);
		free(ac->scs);
		ac->scs = NULL;
	}
}
#endif
