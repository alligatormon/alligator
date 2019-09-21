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

#include <net/if.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define d64 PRId64
#define u64 PRIu64
#define d32 PRId32
#define u32 PRIu32

#include "metric/labels.h"
#include "main.h"

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
	metric_add_labels("swap_usage", &usedswap, DATATYPE_INT, 0, "type", "usage");
	metric_add_labels("swap_usage", &totalswap, DATATYPE_INT, 0, "type", "total");
	metric_add_labels("swap_usage", &availswap, DATATYPE_INT, 0, "type", "avail");
	kvm_close(kd);
}
void get_disk()
{
	struct statfs* mounts;
	int64_t num_mounts = getmntinfo(&mounts, MNT_WAIT);

	metric_add_auto("mounts_num", &num_mounts, DATATYPE_INT, 0);
	for (int i = 0; i < num_mounts; i++) {
		int64_t avail = 0;
		int64_t total = 0;
		int64_t used = 0;
		struct statfs stats;
		if (0 == statfs(mounts[i].f_mntonname, &stats))
		{
			avail = (int64_t)stats.f_bsize * stats.f_bavail;
			total = (int64_t)stats.f_bsize * stats.f_blocks;
			used = total - avail;
		}
		int one = 1;
		metric_add_labels2("disk_usage", &avail, DATATYPE_INT, 0, "mountpoint", mounts[i].f_mntonname, "type", "avail");
		metric_add_labels2("disk_usage", &total, DATATYPE_INT, 0, "mountpoint", mounts[i].f_mntonname, "type", "total");
		metric_add_labels2("disk_usage", &used, DATATYPE_INT, 0, "mountpoint", mounts[i].f_mntonname, "type", "used");
		metric_add_labels2("disk_files", &stats.f_files, DATATYPE_INT, 0, "mountpoint", mounts[i].f_mntonname, "type", "inodes");
		metric_add_labels2("disk_files", &stats.f_ffree, DATATYPE_INT, 0, "mountpoint", mounts[i].f_mntonname, "type", "freeinodes");
		metric_add_labels2("disk_files", &one, DATATYPE_INT, 0, "mountpoint", mounts[i].f_mntonname, "fs", mounts[i].f_fstypename);
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

	u_int inact_size;
	rc = sysctlbyname("vm.stats.vm.v_inactive_count", &inact_size, &uint_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}

	u_int wire_size;
	rc = sysctlbyname("vm.stats.vm.v_wire_count", &wire_size, &uint_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}

	int64_t vm_faults;
	rc = sysctlbyname("vm.stats.vm.v_vm_faults", &vm_faults, &uint_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	int64_t io_faults;
	rc = sysctlbyname("vm.stats.vm.v_io_faults", &io_faults, &uint_size, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	int64_t free_memory = vmt.t_free * (u_int64_t)page_size;
	int64_t avail_memory = vmt.t_avm * (u_int64_t)page_size;
	int64_t active_memory = vmt.t_arm * (u_int64_t)page_size;
	int64_t real_memory = vmt.t_rm * (u_int64_t)page_size;
	int64_t inact_memory = inact_size * (u_int64_t)page_size;
	int64_t wire_memory = wire_size * (u_int64_t)page_size;
	metric_add_labels("memory_usage", &free_memory, DATATYPE_INT, 0, "type", "free");
	metric_add_labels("memory_usage", &wire_memory, DATATYPE_INT, 0, "type", "wire");
	metric_add_labels("memory_usage", &inact_memory, DATATYPE_INT, 0, "type", "inactive");
	metric_add_labels("memory_usage", &real_memory, DATATYPE_INT, 0, "type", "real");
	metric_add_labels("memory_usage", &active_memory, DATATYPE_INT, 0, "type", "active");
	metric_add_labels("memory_usage", &avail_memory, DATATYPE_INT, 0, "type", "available");
	metric_add_labels("load_average", &load1, DATATYPE_DOUBLE, 0, "type", "load1");
	metric_add_labels("load_average", &load5, DATATYPE_DOUBLE, 0, "type", "load5");
	metric_add_labels("load_average", &load15, DATATYPE_DOUBLE, 0, "type", "load15");
	metric_add_auto("vm_faults", &avail_memory, DATATYPE_INT, 0);
	metric_add_auto("io_faults", &avail_memory, DATATYPE_INT, 0);
	metric_add_auto("cores_num", &num_cpu, DATATYPE_INT, 0);
	metric_add_auto("effective_cores_num", &num_cpu, DATATYPE_INT, 0);
}

void get_cpu()
{
	kvm_t *kd;
	kd = kvm_open(NULL, NULL, NULL, O_RDONLY, "kvm");
	if (kd)
	{
		int maxcpu;
		maxcpu = kvm_getmaxcpu(kd);
		//printf("maxcpu=%d\n", maxcpu);
		if (maxcpu > 0) {
			int i,n;
			struct pcpu **cpu1;
			struct pcpu **cpu2;
			cpu1 = calloc(sizeof(struct pcpu*), maxcpu);
			for (n = 0; n < maxcpu; n ++)
				cpu1[n] = kvm_getpcpu(kd, n);
			sleep(1);
			cpu2 = calloc(sizeof(struct pcpu*), maxcpu);
			for (n = 0; n < maxcpu; n ++)
				cpu2[n] = kvm_getpcpu(kd, n);
			for (n = 0; n < maxcpu; n ++)
				if (cpu2[n] && cpu1[n])
				{
					long long total = 0;
					for (i = 0; i < CPUSTATES; i ++)
					{
						cpu2[n]->pc_cp_time[i] -= cpu1[n]->pc_cp_time[i];
						total += cpu2[n]->pc_cp_time[i];
					}
					//printf("CPU #%02d: user=%ld, sys=%ld, nice=%ld, intr=%ld, idle=%ld, total=%lld\n",
					//	cpu2[n]->pc_cpuid, cpu2[n]->pc_cp_time[CP_USER], cpu2[n]->pc_cp_time[CP_SYS],
					//	cpu2[n]->pc_cp_time[CP_NICE], cpu2[n]->pc_cp_time[CP_INTR],
					//	cpu2[n]->pc_cp_time[CP_IDLE], total);
					//printf("CPU #%02d: user=%0.2f%%, sys=%0.2f%%, nice=%0.2f%%, intr=%0.2f%%, idle=%0.2f%%\n",
					//	cpu2[n]->pc_cpuid, cpu2[n]->pc_cp_time[CP_USER]*100.0/total, cpu2[n]->pc_cp_time[CP_SYS]*100.0/total,
					//	cpu2[n]->pc_cp_time[CP_NICE]*100.0/total, cpu2[n]->pc_cp_time[CP_INTR]*100.0/total,
					//	cpu2[n]->pc_cp_time[CP_IDLE]*100.0/total);

					char cpuname[20];
					snprintf(cpuname, 20, "%d", cpu2[n]->pc_cpuid);
					double user = cpu2[n]->pc_cp_time[CP_USER]*100.0/total;
					double system = cpu2[n]->pc_cp_time[CP_SYS]*100.0/total;
					double nice = cpu2[n]->pc_cp_time[CP_NICE]*100.0/total;
					double intr = cpu2[n]->pc_cp_time[CP_INTR]*100.0/total;
					double idle = cpu2[n]->pc_cp_time[CP_IDLE]*100.0/total;
					metric_add_labels2("cpu_usage", &total, DATATYPE_DOUBLE, 0, "core", cpuname, "type", "total");
					metric_add_labels2("cpu_usage", &user, DATATYPE_DOUBLE, 0, "core", cpuname, "type", "user");
					metric_add_labels2("cpu_usage", &system,  DATATYPE_DOUBLE, 0, "core", cpuname, "type", "system");
					metric_add_labels2("cpu_usage", &nice, DATATYPE_DOUBLE, 0, "core", cpuname, "type", "nice");
					metric_add_labels2("cpu_usage", &intr, DATATYPE_DOUBLE, 0, "core", cpuname, "type", "intr");
					metric_add_labels2("cpu_usage", &idle, DATATYPE_DOUBLE, 0, "core", cpuname, "type", "idle");
				}
			for (n = 0; n < maxcpu; n ++)
			{
				if (cpu1[n]) free(cpu1[n]);
				if (cpu2[n]) free(cpu2[n]);
			}
			free(cpu1);
			free(cpu2);
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


void get_proc_info()
{
	//pid_t pid = 1;
	int cntp = 0, i;
	struct kinfo_proc *proc = kinfo_getallproc(&cntp);
	//printf("cntp = %d\n", cntp);
	for (i=0; i<cntp; i++ )
	{
		int64_t val = getpcpu(&(proc[i]));
		metric_add_labels("process_cpu", &val, DATATYPE_INT, 0, "name", proc[i].ki_comm);
		val = proc[i].ki_size;
		metric_add_labels2("process_memory", &val, DATATYPE_INT, 0, "name", proc[i].ki_comm, "type", "vsz");
		val = proc[i].ki_rssize;
		metric_add_labels2("process_memory", &val, DATATYPE_INT, 0, "name", proc[i].ki_comm, "type", "rss");
		val = proc[i].ki_cow;
		metric_add_labels2("process_stats", &val, DATATYPE_INT, 0, "name", proc[i].ki_comm, "type", "COWfaults");
		val = proc[i].ki_numthreads;
		metric_add_labels2("process_stats", &val, DATATYPE_INT, 0, "name", proc[i].ki_comm, "type", "threads");
	}
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
	metric_add_auto("maxsockets", &val, DATATYPE_INT, 0);

	rc = sysctlbyname("kern.ipc.numopensockets", &val, &msz, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	metric_add_auto("opensockets", &val, DATATYPE_INT, 0);

	rc = sysctlbyname("kern.maxproc", &val, &msz, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	metric_add_auto("maxproc", &val, DATATYPE_INT, 0);

	rc = sysctlbyname("kern.maxfiles", &val, &msz, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	metric_add_auto("maxfiles", &val, DATATYPE_INT, 0);

	rc = sysctlbyname("kern.openfiles", &val, &msz, NULL, 0);
	if (rc < 0){
		perror("sysctlbyname");
	}
	metric_add_auto("openfiles", &val, DATATYPE_INT, 0);
}

void disk_io_stats() {
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

		metric_add_labels2("disk_io", &bytes_read, DATATYPE_INT, 0, "dev", current.dinfo->devices[i].device_name, "type", "bytes_read");
		metric_add_labels2("disk_io", &bytes_write, DATATYPE_INT, 0, "dev", current.dinfo->devices[i].device_name, "type", "bytes_write");
		metric_add_labels2("disk_io", &bytes_free, DATATYPE_INT, 0, "dev", current.dinfo->devices[i].device_name, "type", "bytes_free");
		metric_add_labels2("disk_io", &transfers_read, DATATYPE_INT, 0, "dev", current.dinfo->devices[i].device_name, "type", "transfers_read");
		metric_add_labels2("disk_io", &transfers_write, DATATYPE_INT, 0, "dev", current.dinfo->devices[i].device_name, "type", "transfers_write");
		metric_add_labels2("disk_io", &transfers_free, DATATYPE_INT, 0, "dev", current.dinfo->devices[i].device_name, "type", "transfers_free");
	}

	int64_t disknum = current.dinfo->numdevs;
	metric_add_auto("disk_num", &disknum, DATATYPE_INT, 0);
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
			metric_add_labels2("if_stat", &ibytes, DATATYPE_INT, 0, "ifname", ifa->ifa_name, "type", "received_bytes");
			metric_add_labels2("if_stat", &obytes, DATATYPE_INT, 0, "ifname", ifa->ifa_name, "type", "transmit_bytes");
			metric_add_labels2("if_stat", &ipackets, DATATYPE_INT, 0, "ifname", ifa->ifa_name, "type", "received_packages");
			metric_add_labels2("if_stat", &opackets, DATATYPE_INT, 0, "ifname", ifa->ifa_name, "type", "transmit_packages");
			metric_add_labels2("if_stat", &ierrors, DATATYPE_INT, 0, "ifname", ifa->ifa_name, "type", "received_err");
			metric_add_labels2("if_stat", &oerrors, DATATYPE_INT, 0, "ifname", ifa->ifa_name, "type", "transmit_err");
			metric_add_labels2("if_stat", &iqdrops, DATATYPE_INT, 0, "ifname", ifa->ifa_name, "type", "received_drop");
# if __FreeBSD__ > 10
			metric_add_labels2("if_stat", &oqdrops, DATATYPE_INT, 0, "ifname", ifa->ifa_name, "type", "transmit_drop");
# endif
			metric_add_labels2("if_stat", &imcasts, DATATYPE_INT, 0, "ifname", ifa->ifa_name, "type", "received_multicast");
			metric_add_labels2("if_stat", &omcasts, DATATYPE_INT, 0, "ifname", ifa->ifa_name, "type", "transmit_multicast");
			metric_add_labels2("if_stat", &collisions, DATATYPE_INT, 0, "ifname", ifa->ifa_name, "type", "collisions");
			metric_add_labels2("if_stat", &noproto, DATATYPE_INT, 0, "ifname", ifa->ifa_name, "type", "noproto");
			metric_add_labels2("if_stat", &link_state, DATATYPE_INT, 0, "ifname", ifa->ifa_name, "type", "link_state");
		}
	}
	freeifaddrs(ifap);
}

void get_system_metrics()
{
	extern aconf *ac;
	if (ac->system_base)
	{
		get_swap();
		get_mem();
		get_cpu();
		get_fd_info();
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
		get_proc_info();
	}
}

void system_fast_scrape()
{
}
#endif
