#ifdef __FreeBSD__
#include <inttypes.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <unistd.h>
#include <libproc.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/pcpu.h>
#include <kvm.h>
#include <sys/types.h>
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/user.h>
#include <inttypes.h>
#include <sys/sysctl.h>
#include <math.h>
#include <devstat.h>
#include <fcntl.h>
#include <libgeom.h>
#include <limits.h>
#include <sys/vmmeter.h>

#include <net/if.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/jail.h>
#include <jail.h>
#include <sys/rctl.h>

#define d64 PRId64
#define u64 PRIu64
#define d32 PRId32
#define u32 PRIu32

#include "metric/labels.h"
#include "main.h"
extern aconf *ac;

void get_swap()
{
	struct kvm_swap swap;
	kvm_t *kd;
	int64_t totalswap, usedswap, availswap;

	kd = kvm_open(NULL, NULL, NULL, O_RDONLY, "kvm_open");
	if (!kd)
		return;
	kvm_getswapinfo(kd, &swap, 1, 0);
	totalswap = swap.ksw_total;
	usedswap = swap.ksw_used;
	totalswap *= getpagesize() / 1024;
	usedswap *= getpagesize() / 1024;
	availswap = totalswap - usedswap;
	metric_add_labels("swap_usage", &usedswap, DATATYPE_INT, ac->system_carg, "type", "usage");
	metric_add_labels("swap_usage", &totalswap, DATATYPE_INT, ac->system_carg, "type", "total");
	metric_add_labels("swap_usage", &availswap, DATATYPE_INT, ac->system_carg, "type", "avail");
	kvm_close(kd);
}
void get_disk()
{
	struct statfs* mounts;
	int64_t num_mounts = getmntinfo(&mounts, MNT_WAIT);

	metric_add_auto("mounts_num", &num_mounts, DATATYPE_INT, ac->system_carg);
	for (int i = 0; i < num_mounts; i++) {
		int64_t avail = 0;
		int64_t total = 0;
		int64_t used = 0;
		struct statfs stats;
		if (!statfs(mounts[i].f_mntonname, &stats))
		{
			if (!strncmp(mounts[i].f_mntonname, "/etc", 4) || !strncmp(mounts[i].f_mntonname, "/dev", 4) || !strncmp(mounts[i].f_mntonname, "/proc", 5) || !strncmp(mounts[i].f_mntonname, "/sys", 4) || !strncmp(mounts[i].f_mntonname, "/run", 4) || !strncmp(mounts[i].f_mntonname, "/var/lib/docker", 15))
				continue;

			avail = (int64_t)stats.f_bsize * stats.f_bavail;
			total = (int64_t)stats.f_bsize * stats.f_blocks;
			used = total - avail;

			double pused = (double)used*100/(double)total;
			double pfree = 100 - pused;

			metric_add_labels2("disk_usage", &avail, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "avail");
			metric_add_labels2("disk_usage", &total, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "total");
			metric_add_labels2("disk_usage", &used, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "used");
			metric_add_labels2("disk_usage_percent", &pused, DATATYPE_DOUBLE, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "used");
			metric_add_labels2("disk_usage_percent", &pfree, DATATYPE_DOUBLE, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "free");

			uint64_t inodes_used = stats.f_files - stats.f_ffree;
			double iused = inodes_used*100.0/stats.f_files;
			double ifree = 100.0 - iused;
			metric_add_labels2("disk_inodes", &stats.f_ffree, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "free");
			metric_add_labels2("disk_inodes", &inodes_used, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "used");
			metric_add_labels2("disk_inodes", &stats.f_files, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "total");
			metric_add_labels2("disk_inodes_percent", &iused, DATATYPE_DOUBLE, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "used");
			metric_add_labels2("disk_inodes_percent", &ifree, DATATYPE_DOUBLE, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "free");

			int64_t one = 1;
			metric_add_labels2("disk_filesystem", &one, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "fs", mounts[i].f_fstypename);
		}

		//metric_add_labels2("disk_files", &stats.f_files, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "inodes");
		//metric_add_labels2("disk_files", &stats.f_ffree, DATATYPE_INT, ac->system_carg, "mountpoint", mounts[i].f_mntonname, "type", "freeinodes");

	}

}

void get_mem()
{
	int rc;
	u_int page_size;
	struct vmtotal vmt;
	size_t vmt_size, uint_size;

	vmt_size = sizeof(vmt);
	uint_size = sizeof(page_size);
	size_t int64_size = sizeof(int64_t);
	int mib[4];
	int64_t num_cpu;
	size_t len = sizeof(num_cpu);

	mib[0] = CTL_HW;
	mib[1] = HW_NCPU;

	sysctl(mib, 2, &num_cpu, &len, NULL, 0);

	if (num_cpu < 1)
	{
		 mib[1] = HW_NCPU;
		 sysctl(mib, 2, &num_cpu, &len, NULL, 0);
		 if (num_cpu < 1)
			num_cpu = 1;
	}

	mib[0] = CTL_VM;
	mib[1] = VM_LOADAVG;
	struct loadavg load;
	double load1;
	double load5;
	double load15;
	len = sizeof(load);
	sysctl(mib, 2, &load, &len, NULL, 0);
	load1 = (double)load.ldavg[0] / (double)load.fscale;
	load5 = (double)load.ldavg[1] / (double)load.fscale;
	load15 = (double)load.ldavg[2] / (double)load.fscale;

	rc = sysctlbyname("vm.vmtotal", &vmt, &vmt_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}

	rc = sysctlbyname("vm.stats.vm.v_page_size", &page_size, &uint_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}

	u_int wire_size;
	rc = sysctlbyname("vm.stats.vm.v_wire_count", &wire_size, &uint_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}

	int64_t vm_faults;
	rc = sysctlbyname("vm.stats.vm.v_vm_faults", &vm_faults, &int64_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	int64_t io_faults;
	rc = sysctlbyname("vm.stats.vm.v_io_faults", &io_faults, &int64_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	int64_t v_swapout;
	rc = sysctlbyname("vm.stats.vm.v_swapout", &v_swapout, &int64_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	int64_t v_swapin;
	rc = sysctlbyname("vm.stats.vm.v_swapin", &v_swapin, &int64_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	int64_t v_swappgsout;
	rc = sysctlbyname("vm.stats.vm.v_swappgsout", &v_swapout, &int64_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	int64_t v_swappgsin;
	rc = sysctlbyname("vm.stats.vm.v_swappgsin", &v_swapin, &int64_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	int64_t physmem;
	rc = sysctlbyname("hw.physmem", &physmem, &int64_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	int64_t v_active_count;
	rc = sysctlbyname("vm.stats.vm.v_active_count", &v_active_count, &int64_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	int64_t v_inactive_count;
	rc = sysctlbyname("vm.stats.vm.v_inactive_count", &v_inactive_count, &int64_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	int64_t free_memory = vmt.t_free * (u_int64_t)page_size;
	int64_t avail_memory = vmt.t_avm * (u_int64_t)page_size;
	int64_t active_memory = v_active_count * (u_int64_t)page_size;
	int64_t inact_memory = v_inactive_count * page_size;
	int64_t real_memory = vmt.t_rm * (u_int64_t)page_size;
	int64_t wire_memory = wire_size * (u_int64_t)page_size;
	//printf("vm.stats.vm.v_inactive_count: %ld, inact: %ld, page_size: %u\n", v_inactive_count, inact_memory, page_size);

	metric_add_labels("memory_usage", &physmem, DATATYPE_INT, ac->system_carg, "type", "total");
	metric_add_labels("memory_usage", &free_memory, DATATYPE_INT, ac->system_carg, "type", "free");
	metric_add_labels("memory_usage", &wire_memory, DATATYPE_INT, ac->system_carg, "type", "wire");
	metric_add_labels("memory_usage", &inact_memory, DATATYPE_INT, ac->system_carg, "type", "inactive");
	metric_add_labels("memory_usage", &real_memory, DATATYPE_INT, ac->system_carg, "type", "real");
	metric_add_labels("memory_usage", &active_memory, DATATYPE_INT, ac->system_carg, "type", "active");
	metric_add_labels("memory_usage", &avail_memory, DATATYPE_INT, ac->system_carg, "type", "available");

	double percentused = ((double)active_memory+(double)wire_memory)*100/(double)physmem;
	double percentfree = 100 - percentused;
	metric_add_labels("memory_usage_percent", &percentused, DATATYPE_DOUBLE, ac->system_carg, "type", "used");
	metric_add_labels("memory_usage_percent", &percentfree, DATATYPE_DOUBLE, ac->system_carg, "type", "free");

	metric_add_labels("memory_stat", &v_swapout, DATATYPE_INT, ac->system_carg, "type", "pgpgout");
	metric_add_labels("memory_stat", &v_swapin, DATATYPE_INT, ac->system_carg, "type", "pgpgin");
	metric_add_labels("memory_stat", &v_swappgsout, DATATYPE_INT, ac->system_carg, "type", "pgswpout");
	metric_add_labels("memory_stat", &v_swappgsin, DATATYPE_INT, ac->system_carg, "type", "pgswpin");
	metric_add_labels("load_average", &load1, DATATYPE_DOUBLE, ac->system_carg, "type", "load1");
	metric_add_labels("load_average", &load5, DATATYPE_DOUBLE, ac->system_carg, "type", "load5");
	metric_add_labels("load_average", &load15, DATATYPE_DOUBLE, ac->system_carg, "type", "load15");
	metric_add_auto("vm_faults", &avail_memory, DATATYPE_INT, ac->system_carg);
	metric_add_auto("io_faults", &avail_memory, DATATYPE_INT, ac->system_carg);
	metric_add_auto("cores_num", &num_cpu, DATATYPE_INT, ac->system_carg);
	metric_add_auto("effective_cores_num", &num_cpu, DATATYPE_INT, ac->system_carg);
}

void get_sysctl_stat_u64(char *ctlname, char *mapping)
{
	size_t uint_size = sizeof(uint64_t);
	uint64_t val;
	if (sysctlbyname(ctlname, &val, &uint_size, NULL, 0) < 0)
		perror("sysctlbyname");

	metric_add_auto(mapping, &val, DATATYPE_UINT, ac->system_carg);
}

void get_vmstat()
{
	get_sysctl_stat_u64("vm.stats.sys.v_soft", "softirq");
	get_sysctl_stat_u64("vm.stats.sys.v_intr", "interrupts");
	get_sysctl_stat_u64("vm.stats.sys.v_syscall", "system_calls");
	get_sysctl_stat_u64("vm.stats.sys.v_swtch", "context_switches");
	get_sysctl_stat_u64("vm.stats.vm.v_forks", "forks");
	get_sysctl_stat_u64("vm.stats.vm.v_vforks", "vforks");
	get_sysctl_stat_u64("vm.stats.vm.v_rforks", "rforks");
	get_sysctl_stat_u64("kern.openfiles", "open_files");
	get_sysctl_stat_u64("kern.maxfiles", "max_files");
	get_sysctl_stat_u64("kern.maxfilesperproc", "max_files_per_proc");
	get_sysctl_stat_u64("kern.ipc.numopensockets", "open_sockets");
	get_sysctl_stat_u64("kern.ipc.maxsockets", "max_sockets");
}

void get_cpu()
{
	kvm_t *kd;
	kd = kvm_open(NULL, NULL, NULL, O_RDONLY, "kvm");
	if (kd)
	{
		int maxcpu;
		uint64_t current_cores = 0;
		maxcpu = kvm_getmaxcpu(kd);
		//printf("maxcpu=%d\n", maxcpu);
		if (maxcpu > 0)
		{
			uint64_t total = 0;
			uint64_t total_user = 0;
			uint64_t total_system = 0;
			uint64_t total_nice = 0;
			uint64_t total_intr = 0;
			uint64_t total_idle = 0;
			int n;
			struct pcpu **cpu1;
			cpu1 = calloc(sizeof(struct pcpu*), maxcpu);
			for (n = 0; n < maxcpu; n ++)
				cpu1[n] = kvm_getpcpu(kd, n);
			for (n = 0; n < maxcpu; n ++)
			{
				if (cpu1[n])
				{
					char cpuname[20];
					snprintf(cpuname, 20, "%d", cpu1[n]->pc_cpuid);
					uint64_t user = cpu1[n]->pc_cp_time[CP_USER];
					uint64_t system = cpu1[n]->pc_cp_time[CP_SYS];
					uint64_t nice = cpu1[n]->pc_cp_time[CP_NICE];
					uint64_t intr = cpu1[n]->pc_cp_time[CP_INTR];
					uint64_t idle = cpu1[n]->pc_cp_time[CP_IDLE];
					//printf("double: %lf, user: %ld*100.0/%lf\n", total, cpu1[n]->pc_cp_time[CP_USER], total);
					metric_add_labels2("cpu_usage_core_time", &user, DATATYPE_UINT, ac->system_carg, "core", cpuname, "type", "user");
					metric_add_labels2("cpu_usage_core_time", &system,  DATATYPE_UINT, ac->system_carg, "core", cpuname, "type", "system");
					metric_add_labels2("cpu_usage_core_time", &nice, DATATYPE_UINT, ac->system_carg, "core", cpuname, "type", "nice");
					metric_add_labels2("cpu_usage_core_time", &intr, DATATYPE_UINT, ac->system_carg, "core", cpuname, "type", "intr");
					metric_add_labels2("cpu_usage_core_time", &idle, DATATYPE_UINT, ac->system_carg, "core", cpuname, "type", "idle");

					total_user += user;
					total_system += system;
					total_nice += nice;
					total_intr += intr;
					total_idle += idle;
					total += user + system + nice + intr;
					++current_cores;
				}
			}

			for (n = 0; n < maxcpu; n ++)
				if (cpu1[n]) free(cpu1[n]);
			free(cpu1);

			total /= current_cores;
			total_system /= current_cores;
			total_user /= current_cores;
			total_nice /= current_cores;
			total_intr /= current_cores;
			total_idle /= current_cores;

			metric_add_labels("cpu_usage_time", &total,  DATATYPE_UINT, ac->system_carg, "type", "total");
			metric_add_labels("cpu_usage_time", &total_system,  DATATYPE_UINT, ac->system_carg, "type", "system");
			metric_add_labels("cpu_usage_time", &total_user, DATATYPE_UINT, ac->system_carg, "type", "user");
			metric_add_labels("cpu_usage_time", &total_nice, DATATYPE_UINT, ac->system_carg, "type", "nice");
			metric_add_labels("cpu_usage_time", &total_intr, DATATYPE_UINT, ac->system_carg, "type", "intr");
			metric_add_labels("cpu_usage_time", &total_idle, DATATYPE_UINT, ac->system_carg, "type", "idle");
		}
		kvm_close(kd);
	}
}


double getpcpu(const struct kinfo_proc *k)
{
	static int failure;

	int fscale;
	size_t len = sizeof(fscale);
	if (sysctlbyname("kern.fscale", &fscale, &len, NULL, 0) == -1)
		return (1);

	fixpt_t	ccpu;
	len = sizeof(ccpu);
	if (sysctlbyname("kern.ccpu", &ccpu, &len, NULL, 0) == -1)
		return (1);

	if (failure)
		return (0.0);

	if (k->ki_swtime == 0 || (k->ki_flag & P_INMEM) == 0)
		return (0.0);

	return (100.0 * ((double)k->ki_pctcpu/fscale) / (1.0 - exp(k->ki_swtime * log(((double)ccpu/fscale)))));
}


void get_proc_info(int8_t lightweight)
{
	//pid_t pid = 1;
	int cntp = 0, i;
	struct kinfo_proc *proc = kinfo_getallproc(&cntp);
	uint64_t running = 0;
	uint64_t sleeping = 0;
	uint64_t stopped = 0;
	uint64_t zombie = 0;
	uint64_t wait = 0;
	uint64_t locked = 0;
	//printf("cntp = %d\n", cntp);
	for (i=0; i<cntp; i++ )
	{
		if (proc[i].ki_stat == SRUN)
			++running;
		else if (proc[i].ki_stat == SSLEEP)
			++sleeping;
		else if (proc[i].ki_stat == SSTOP)
			++stopped;
		else if (proc[i].ki_stat == SZOMB)
			++zombie;
		else if (proc[i].ki_stat == SWAIT)
			++wait;
		else if (proc[i].ki_stat == SLOCK)
			++locked;

		if (!lightweight)
		{
			int64_t val = getpcpu(&(proc[i]));
			metric_add_labels("process_cpu", &val, DATATYPE_INT, ac->system_carg, "name", proc[i].ki_comm);
			val = proc[i].ki_size;
			metric_add_labels2("process_memory", &val, DATATYPE_INT, ac->system_carg, "name", proc[i].ki_comm, "type", "vsz");
			val = proc[i].ki_rssize;
			metric_add_labels2("process_memory", &val, DATATYPE_INT, ac->system_carg, "name", proc[i].ki_comm, "type", "rss");
			val = proc[i].ki_cow;
			metric_add_labels2("process_stats", &val, DATATYPE_INT, ac->system_carg, "name", proc[i].ki_comm, "type", "COWfaults");
			val = proc[i].ki_numthreads;
			metric_add_labels2("process_stats", &val, DATATYPE_INT, ac->system_carg, "name", proc[i].ki_comm, "type", "threads");
		}
	}

	metric_add_labels("process_states", &running, DATATYPE_UINT, ac->system_carg, "state", "running");
	metric_add_labels("process_states", &sleeping, DATATYPE_UINT, ac->system_carg, "state", "sleeping");
	metric_add_labels("process_states", &stopped, DATATYPE_UINT, ac->system_carg, "state", "stopped");
	metric_add_labels("process_states", &zombie, DATATYPE_UINT, ac->system_carg, "state", "zombie");
	metric_add_labels("process_states", &locked, DATATYPE_UINT, ac->system_carg, "state", "locked");
	metric_add_labels("process_states", &wait, DATATYPE_UINT, ac->system_carg, "state", "wait");

	time_t t = time(NULL);
	struct tm lt = {0};
	localtime_r(&t, &lt);

	r_time time1 = setrtime();
	time1.sec += lt.tm_gmtoff;
	uint64_t uptime = time1.sec - proc[1].ki_start.tv_sec;
	//printf("uptime %llu - %llu = %llu\n", time1.sec, proc[1].ki_start.tv_sec, uptime);
	metric_add_auto("uptime", &uptime, DATATYPE_UINT, ac->system_carg);

	free(proc);
}

void get_fd_info()
{
	int rc;

	uint64_t val;
	size_t msz = sizeof(val);
	rc = sysctlbyname("kern.ipc.maxsockets", &val, &msz, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	metric_add_auto("maxsockets", &val, DATATYPE_INT, ac->system_carg);

	rc = sysctlbyname("kern.ipc.numopensockets", &val, &msz, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	metric_add_auto("opensockets", &val, DATATYPE_INT, ac->system_carg);

	rc = sysctlbyname("kern.maxproc", &val, &msz, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	metric_add_auto("maxproc", &val, DATATYPE_INT, ac->system_carg);

	rc = sysctlbyname("kern.maxfiles", &val, &msz, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	metric_add_auto("maxfiles", &val, DATATYPE_INT, ac->system_carg);

	rc = sysctlbyname("kern.openfiles", &val, &msz, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	metric_add_auto("openfiles", &val, DATATYPE_INT, ac->system_carg);
}

void disk_io_stats()
{
	struct devinfo *info = calloc(1, sizeof(*info));
	struct statinfo current;
	current.dinfo = info;

	if (devstat_getdevs(NULL, &current) == -1) {
		return;
	}

	for (int i = 0; i < current.dinfo->numdevs; i++) {
		int64_t bytes_read, bytes_write, bytes_free;
		int64_t transfers_other, transfers_read, transfers_write, transfers_free;
		double duration_other, duration_read, duration_write, duration_free;
		long double busy_time;
		int64_t blocks;

		//current.dinfo->devices[i].unit_number;
		devstat_compute_statistics(&current.dinfo->devices[i],
				NULL,
				1.0,
				DSM_TOTAL_BYTES_READ, &bytes_read,
				DSM_TOTAL_BYTES_WRITE, &bytes_write,
				DSM_TOTAL_BYTES_FREE, &bytes_free,
				DSM_TOTAL_TRANSFERS_OTHER, &transfers_other,
				DSM_TOTAL_TRANSFERS_READ, &transfers_read,
				DSM_TOTAL_TRANSFERS_WRITE, &transfers_write,
				DSM_TOTAL_TRANSFERS_FREE, &transfers_free,
				DSM_TOTAL_DURATION_OTHER, &duration_other,
				DSM_TOTAL_DURATION_READ, &duration_read,
				DSM_TOTAL_DURATION_WRITE, &duration_write,
				DSM_TOTAL_DURATION_FREE, &duration_free,
				DSM_TOTAL_BUSY_TIME, &busy_time,
				DSM_TOTAL_BLOCKS, &blocks,
				DSM_NONE);

		metric_add_labels2("disk_io", &bytes_read, DATATYPE_INT, ac->system_carg, "dev", current.dinfo->devices[i].device_name, "type", "bytes_read");
		metric_add_labels2("disk_io", &bytes_write, DATATYPE_INT, ac->system_carg, "dev", current.dinfo->devices[i].device_name, "type", "bytes_write");
		metric_add_labels2("disk_io", &bytes_free, DATATYPE_INT, ac->system_carg, "dev", current.dinfo->devices[i].device_name, "type", "bytes_free");
		metric_add_labels2("disk_io", &transfers_read, DATATYPE_INT, ac->system_carg, "dev", current.dinfo->devices[i].device_name, "type", "transfers_read");
		metric_add_labels2("disk_io", &transfers_write, DATATYPE_INT, ac->system_carg, "dev", current.dinfo->devices[i].device_name, "type", "transfers_write");
		metric_add_labels2("disk_io", &transfers_free, DATATYPE_INT, ac->system_carg, "dev", current.dinfo->devices[i].device_name, "type", "transfers_free");
		double d_busy_time = (double)busy_time;
		metric_add_labels("disk_busy", &d_busy_time, DATATYPE_DOUBLE, ac->system_carg, "dev", current.dinfo->devices[i].device_name);
	}

	int64_t disknum = current.dinfo->numdevs;
	metric_add_auto("disk_num", &disknum, DATATYPE_INT, ac->system_carg);
	free(info);
}


void get_iface_statistics()
{
	struct ifaddrs *ifap, *ifa;
	//struct sockaddr_in *sa;
	//char *addr;

	getifaddrs (&ifap);
	for (ifa = ifap; ifa; ifa = ifa->ifa_next)
	{
		//if (ifa->ifa_addr->sa_family == AF_INET)
		//{
		//	sa = (struct sockaddr_in *) ifa->ifa_addr;
		//	addr = inet_ntoa(sa->sin_addr);
		//	printf("Interface: %s\tAddress: %s\n", ifa->ifa_name, addr);
		//}
		//else if (ifa->ifa_addr->sa_family == AF_LINK)
		if (ifa->ifa_addr->sa_family == AF_LINK)
		{
			struct if_data *ifd = ifa->ifa_data;
			int64_t link_state = ifd->ifi_link_state;
			int64_t ipackets = ifd->ifi_ipackets;
			int64_t opackets = ifd->ifi_opackets;
			int64_t ibytes = ifd->ifi_ibytes;
			int64_t obytes = ifd->ifi_obytes;
			int64_t ierrors = ifd->ifi_ierrors;
			int64_t oerrors = ifd->ifi_oerrors;
			int64_t iqdrops = ifd->ifi_iqdrops;
# if __FreeBSD__ > 10
			int64_t oqdrops = ifd->ifi_oqdrops;
# endif
			int64_t imcasts = ifd->ifi_imcasts;
			int64_t omcasts = ifd->ifi_omcasts;
			int64_t noproto = ifd->ifi_noproto;
			int64_t collisions = ifd->ifi_collisions;
			metric_add_labels2("if_stat", &ibytes, DATATYPE_INT, ac->system_carg, "ifname", ifa->ifa_name, "type", "received_bytes");
			metric_add_labels2("if_stat", &obytes, DATATYPE_INT, ac->system_carg, "ifname", ifa->ifa_name, "type", "transmit_bytes");
			metric_add_labels2("if_stat", &ipackets, DATATYPE_INT, ac->system_carg, "ifname", ifa->ifa_name, "type", "received_packets");
			metric_add_labels2("if_stat", &opackets, DATATYPE_INT, ac->system_carg, "ifname", ifa->ifa_name, "type", "transmit_packets");
			metric_add_labels2("if_stat", &ierrors, DATATYPE_INT, ac->system_carg, "ifname", ifa->ifa_name, "type", "received_err");
			metric_add_labels2("if_stat", &oerrors, DATATYPE_INT, ac->system_carg, "ifname", ifa->ifa_name, "type", "transmit_err");
			metric_add_labels2("if_stat", &iqdrops, DATATYPE_INT, ac->system_carg, "ifname", ifa->ifa_name, "type", "received_drop");
# if __FreeBSD__ > 10
			metric_add_labels2("if_stat", &oqdrops, DATATYPE_INT, ac->system_carg, "ifname", ifa->ifa_name, "type", "transmit_drop");
# endif
			metric_add_labels2("if_stat", &imcasts, DATATYPE_INT, ac->system_carg, "ifname", ifa->ifa_name, "type", "received_multicast");
			metric_add_labels2("if_stat", &omcasts, DATATYPE_INT, ac->system_carg, "ifname", ifa->ifa_name, "type", "transmit_multicast");
			metric_add_labels2("if_stat", &collisions, DATATYPE_INT, ac->system_carg, "ifname", ifa->ifa_name, "type", "collisions");
			metric_add_labels2("if_stat", &noproto, DATATYPE_INT, ac->system_carg, "ifname", ifa->ifa_name, "type", "noproto");
			metric_add_labels2("link_status", &link_state, DATATYPE_INT, ac->system_carg, "ifname", ifa->ifa_name, "state", "up");
		}
	}
	freeifaddrs(ifap);
}

static void jail_calc(int jid, char *name)
{
	char q[MAXHOSTNAMELEN + 20], buf[4096];
	//printf("jid:%d  name:%s ", jid, name);

	snprintf(q, sizeof q, "jail:%s:cputime", name);

	rctl_get_racct(q, strlen(q) + 1, buf, sizeof buf);
	//printf("buf: %s\n", buf);
//cputime=174,datasize=798720,stacksize=0,coredumpsize=0,memoryuse=25550848,memorylocked=0,maxproc=5,openfiles=1280,vmemoryuse=296804352,pseudoterminals=0,swapuse=0,nthr=5,msgqqueued=0,msgqsize=0,nmsgq=0,nsem=0,nsemop=0,nshm=0,shmsize=0,wallclock=291876,pcpu=0,readbps=0,writebps=0,readiops=0,writeiops=0

	char *tmp = buf;
	char *tmp2;
	while (tmp)
	{
		tmp2 = strstr(tmp, "=");
		//printf("test: '%s' (%zu)\n", tmp, tmp2-tmp);

		if (!strncmp(tmp, "cputime", tmp2-tmp))
		{
			tmp2 += strcspn(tmp2, "0123456789");
			uint64_t val = strtoull(tmp2, &tmp2, 10);
			//printf("container_cpu_usage_seconds_total {name='%s'} %lu\n", name, val);
			metric_add_labels("container_cpu_usage_seconds_total", &val, DATATYPE_UINT, ac->system_carg, "name", name);
		}
		else if (!strncmp(tmp, "memoryuse", tmp2-tmp))
		{
			tmp2 += strcspn(tmp2, "0123456789");
			uint64_t val = strtoull(tmp2, &tmp2, 10);
			//printf("container_memory_rss {name='%s'} %lu\n", name, val);
			metric_add_labels("container_memory_rss", &val, DATATYPE_UINT, ac->system_carg, "name", name);
		}
		else if (!strncmp(tmp, "vmemoryuse", tmp2-tmp))
		{
			tmp2 += strcspn(tmp2, "0123456789");
			uint64_t val = strtoull(tmp2, &tmp2, 10);
			//printf("container_memory_working_set_bytes {name='%s'} %lu\n", name, val);
			metric_add_labels("container_memory_working_set_bytes", &val, DATATYPE_UINT, ac->system_carg, "name", name);
		}
		else if (!strncmp(tmp, "swapuse", tmp2-tmp))
		{
			tmp2 += strcspn(tmp2, "0123456789");
			uint64_t val = strtoull(tmp2, &tmp2, 10);
			//printf("container_memory_swap {name='%s'} %lu\n", name, val);
			metric_add_labels("container_memory_swap", &val, DATATYPE_UINT, ac->system_carg, "name", name);
		}
		else if (!strncmp(tmp, "pcpu", tmp2-tmp))
		{
			tmp2 += strcspn(tmp2, "0123456789");
			uint64_t val = strtoull(tmp2, &tmp2, 10);
			//printf("container_cpu_load_average_10s {name='%s'} %lu\n", name, val);
			metric_add_labels("container_cpu_load_average_10s", &val, DATATYPE_UINT, ac->system_carg, "name", name);
		}
		else if (!strncmp(tmp, "readbps", tmp2-tmp))
		{
			tmp2 += strcspn(tmp2, "0123456789");
			uint64_t val = strtoull(tmp2, &tmp2, 10);
			//printf("container_fs_reads_bytes_per_second {name='%s'} %lu\n", name, val);
			metric_add_labels("container_fs_reads_bytes_per_second", &val, DATATYPE_UINT, ac->system_carg, "name", name);
		}
		else if (!strncmp(tmp, "writebps", tmp2-tmp))
		{
			tmp2 += strcspn(tmp2, "0123456789");
			uint64_t val = strtoull(tmp2, &tmp2, 10);
			//printf("container_fs_writes_bytes_per_second {name='%s'} %lu\n", name, val);
			metric_add_labels("container_fs_writes_bytes_per_second", &val, DATATYPE_UINT, ac->system_carg, "name", name);
		}
		else if (!strncmp(tmp, "readiops", tmp2-tmp))
		{
			tmp2 += strcspn(tmp2, "0123456789");
			uint64_t val = strtoull(tmp2, &tmp2, 10);
			//printf("container_fs_sector_reads_per_second {name='%s'} %lu\n", name, val);
			metric_add_labels("container_fs_sector_reads_per_second", &val, DATATYPE_UINT, ac->system_carg, "name", name);
		}
		else if (!strncmp(tmp, "writeiops", tmp2-tmp))
		{
			tmp2 += strcspn(tmp2, "0123456789");
			uint64_t val = strtoull(tmp2, &tmp2, 10);
			//printf("container_fs_sector_writes_per_second {name='%s'} %lu\n", name, val);
			metric_add_labels("container_fs_sector_writes_per_second", &val, DATATYPE_UINT, ac->system_carg, "name", name);
		}
		tmp = strstr(tmp2, ",");
		if (tmp)
			++tmp;
	}
}

void get_jail_stat()
{
	int ena;
	size_t ena_len = sizeof ena;
	int rv = sysctlbyname("kern.racct.enable", &ena, &ena_len, 0, 0);
	if (rv == -1 && errno == ENOENT)
	{
		puts("RACCT/RCTL support not compiled; see rctl(8)");
		return;
	}
	if (!ena)
	{
		puts("RACCT/RCTL support not enabled; enable using kern.racct.enable=1 tunable");
		return;
	}

	int jid, lastjid;
	char name[MAXHOSTNAMELEN];
	struct jailparam param[3];

	if (jailparam_init(&param[0], "lastjid") == -1 || jailparam_init(&param[1], "jid") == -1 || jailparam_init(&param[2], "name") == -1)
	{
		printf("jailparam_init: %s\n", jail_errmsg);
		return;
	}

	if (jailparam_import_raw(&param[0], &lastjid, sizeof lastjid) == -1 || jailparam_import_raw(&param[1], &jid, sizeof jid) == -1 || jailparam_import_raw(&param[2], name, sizeof name) == -1)
	{
		printf("jailparam_import_raw: %s\n", jail_errmsg);
		return;
	}

	int jflags = 0, n;
	lastjid = n = 0;
	do
	{
		rv = jailparam_get(param, sizeof (param) / sizeof (param[0]), jflags);
		if (rv != -1)
			jail_calc(*(int*)param[1].jp_value, (char*)param[2].jp_value);

		lastjid = rv;
	}
	while (rv >= 0);
}

void get_system_metrics()
{
	if (ac->system_base)
	{
		get_swap();
		get_mem();
		get_cpu();
		get_fd_info();
		get_vmstat();
	}
	if (ac->system_base && !ac->system_process)
	{
		get_proc_info(1);
	}
	if (ac->system_network)
	{
		get_iface_statistics();
	}
	if (ac->system_disk)
	{
		get_disk();
		disk_io_stats();
	}
	if (ac->system_process)
	{
		get_proc_info(0);
	}
	if (ac->system_cadvisor)
	{
		get_jail_stat();
	}
	if (ac->system_cadvisor)
	{
		get_jail_stat();
	}
}

void system_fast_scrape()
{
}

void system_slow_scrape()
{
	if (ac->system_base)
	{
		get_smbios();
	}
}

void userprocess_push(alligator_ht *userprocess, char *user)
{
}

void userprocess_del(alligator_ht* userprocess, char *user)
{
}

void pidfile_push(char *file, int type)
{
}

void pidfile_del(char *file, int type)
{
}
#endif
