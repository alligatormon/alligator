#ifdef __APPLE__
#include <inttypes.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <unistd.h>
#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <mach/mach.h>
#include <libproc.h>
#include <mach/mach_time.h>
#include <mach-o/ldsyms.h>

#include "main.h"

void get_swap()
{
	struct xsw_usage vmusage = {0};
	size_t size = sizeof(vmusage);
	if( sysctlbyname("vm.swapusage", &vmusage, &size, NULL, 0)!=0 )
	{
		perror( "unable to get swap usage by calling sysctlbyname(\"vm.swapusage\",...)" );
	}

	metric_labels_add_lbl("swap_usage", &vmusage.xsu_total, ALLIGATOR_DATATYPE_INT, 0, "type", "total");
	metric_labels_add_lbl("swap_usage", &vmusage.xsu_avail, ALLIGATOR_DATATYPE_INT, 0, "type", "avail");
	metric_labels_add_lbl("swap_usage", &vmusage.xsu_used, ALLIGATOR_DATATYPE_INT, 0, "type", "used");
}
void get_disk()
{
	struct statfs* mounts;
	int num_mounts = getmntinfo(&mounts, MNT_WAIT);
	
	metric_labels_add_auto("mounts_num", &num_mounts, ALLIGATOR_DATATYPE_INT, 0);
	for (int i = 0; i < num_mounts; i++) {
		uint64_t disksize = 0;
		struct statfs stats;
		if (0 == statfs(mounts[i].f_mntonname, &stats))
		{
			disksize = (uint64_t)stats.f_bsize * stats.f_bfree;
		}
		int one = 1;
		metric_labels_add_lbl("disk_usage", &disksize, ALLIGATOR_DATATYPE_INT, 0, "mountpoint", mounts[i].f_mntonname);
		metric_labels_add_lbl2("disk_files", &stats.f_files, ALLIGATOR_DATATYPE_INT, 0, "mountpoint", mounts[i].f_mntonname, "type", "inodes");
		metric_labels_add_lbl2("disk_files", &stats.f_ffree, ALLIGATOR_DATATYPE_INT, 0, "mountpoint", mounts[i].f_mntonname, "type", "freeinodes");
		metric_labels_add_lbl2("disk_files", &one, ALLIGATOR_DATATYPE_INT, 0, "mountpoint", mounts[i].f_mntonname, "fs", mounts[i].f_fstypename);
	}

}

void get_mem()
{
	int mib[2];
	int64_t physical_memory;
	mib[0] = CTL_HW;
	mib[1] = HW_MEMSIZE;
	size_t length = sizeof(int64_t);
	sysctl(mib, 2, &physical_memory, &length, NULL, 0);

	vm_size_t page_size;
	mach_port_t mach_port;
	mach_msg_type_number_t count;
	vm_statistics64_data_t vm_stats;

	mach_port = mach_host_self();
	count = sizeof(vm_stats) / sizeof(natural_t);
	if (KERN_SUCCESS == host_page_size(mach_port, &page_size) && KERN_SUCCESS == host_statistics64(mach_port, HOST_VM_INFO, (host_info64_t)&vm_stats, &count))
	{
		int64_t free_memory = (int64_t)vm_stats.free_count * (int64_t)page_size;

		int64_t used_memory =	((int64_t)vm_stats.active_count +
					(int64_t)vm_stats.inactive_count +
					(int64_t)vm_stats.wire_count) * (int64_t)page_size;
		int64_t active_memory = (int64_t)vm_stats.active_count*(int64_t)page_size;
		int64_t inactive_memory = (int64_t)vm_stats.inactive_count*(int64_t)page_size;
		int64_t wire_memory = (int64_t)vm_stats.wire_count*(int64_t)page_size;
		metric_labels_add_lbl("memory_usage", &free_memory, ALLIGATOR_DATATYPE_INT, 0, "type", "free");
		metric_labels_add_lbl("memory_usage", &used_memory, ALLIGATOR_DATATYPE_INT, 0, "type", "used");
		metric_labels_add_lbl("memory_usage", &active_memory, ALLIGATOR_DATATYPE_INT, 0, "type", "active");
		metric_labels_add_lbl("memory_usage", &inactive_memory, ALLIGATOR_DATATYPE_INT, 0, "type", "inactive");
		metric_labels_add_lbl("memory_usage", &wire_memory, ALLIGATOR_DATATYPE_INT, 0, "type", "wire");
	}
}

typedef struct cpu_counters
{
	uint64_t totalSystemTime;
	uint64_t totalUserTime;
	uint64_t totalIdleTime;

} cpu_counters;

int get_cpu_counters(struct cpu_counters *cpu_counters, struct cpu_counters** cores)
{
	processor_cpu_load_info_t cpuLoad;
	mach_msg_type_number_t processorMsgCount;
	natural_t processorCount;

	uint64_t totalSystemTime = 0, totalUserTime = 0, totalIdleTime = 0;

	/*kern_return_t err =*/ host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &processorCount, (processor_info_array_t *)&cpuLoad, &processorMsgCount);
	*cores = calloc(sizeof(struct cpu_counters), processorCount);

	for (int i = 0; i < processorCount; i++) {

		uint64_t system = 0, user = 0, idle = 0;

		system = cpuLoad[i].cpu_ticks[CPU_STATE_SYSTEM];
		user = cpuLoad[i].cpu_ticks[CPU_STATE_USER] + cpuLoad[i].cpu_ticks[CPU_STATE_NICE];
		idle = cpuLoad[i].cpu_ticks[CPU_STATE_IDLE];
		(*cores)[i].totalSystemTime = system;
		(*cores)[i].totalUserTime = user;
		(*cores)[i].totalIdleTime = idle;

		totalSystemTime += system;
		totalUserTime += user;
		totalIdleTime += idle;
	}
	cpu_counters->totalSystemTime = totalSystemTime;
	cpu_counters->totalUserTime = totalUserTime;
	cpu_counters->totalIdleTime = totalIdleTime;
	return processorCount;
}

void get_cpu()
{
	struct cpu_counters delta;
	struct cpu_counters getcpu1_counters;
	struct cpu_counters getcpu2_counters;
	struct cpu_counters *cores1;
	struct cpu_counters *cores2;
	int64_t cores_num;

	cores_num = get_cpu_counters(&getcpu1_counters, &cores1);
	sleep(1);
	get_cpu_counters(&getcpu2_counters, &cores2);

	delta.totalSystemTime = getcpu2_counters.totalSystemTime - getcpu1_counters.totalSystemTime;
	delta.totalUserTime = getcpu2_counters.totalUserTime - getcpu1_counters.totalUserTime;
	delta.totalIdleTime = getcpu2_counters.totalIdleTime - getcpu1_counters.totalIdleTime;

	uint64_t total = delta.totalSystemTime + delta.totalUserTime + delta.totalIdleTime;
	double onePercent = total/100.0f;
	double totalSystemTime = delta.totalSystemTime/(double)onePercent;
	double totalUserTime = delta.totalUserTime/(double)onePercent;
	double totalIdleTime = delta.totalIdleTime/(double)onePercent;

	metric_labels_add_lbl("cpu_usage", &totalSystemTime, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "system");
	metric_labels_add_lbl("cpu_usage", &totalUserTime, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "user");
	metric_labels_add_lbl("cpu_usage", &totalIdleTime, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "idle");
	metric_labels_add_auto("cores_num", &cores_num, ALLIGATOR_DATATYPE_INT, 0);
	int i;
	char *corename;
	for ( i=0; i<cores_num; i++ )
	{
		int64_t csystem = cores2[i].totalSystemTime - cores1[i].totalSystemTime;
		int64_t cuser = cores2[i].totalUserTime - cores1[i].totalUserTime;
		int64_t cidle = cores2[i].totalIdleTime - cores1[i].totalIdleTime;
		total = csystem+cuser+cidle;
		onePercent = total/100.0f;
		double system = csystem/onePercent;
		double user = cuser/onePercent;
		double idle = cidle/onePercent;
		
		corename = malloc(20);
		snprintf(corename, 20, "cpu%d", i);
		metric_labels_add_lbl2("core_usage", &system, ALLIGATOR_DATATYPE_DOUBLE, 0, "core", corename, "type", "system");
		metric_labels_add_lbl2("core_usage", &user, ALLIGATOR_DATATYPE_DOUBLE, 0, "core", corename, "type", "user");
		metric_labels_add_lbl2("core_usage", &idle, ALLIGATOR_DATATYPE_DOUBLE, 0, "core", corename, "type", "idle");
	}
	free(cores1);
	free(cores2);
}

int get_proc_info()
{
	int bufsize = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
	pid_t pids[2 * bufsize / sizeof(pid_t)];
	bufsize = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
	size_t num_pids = bufsize / sizeof(pid_t);

	for( int index=0; index < num_pids; index++)
	{
		pid_t pid = pids[index] ;

		struct proc_taskinfo taskInfo ;
		struct proc_bsdinfo proc;
		proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &taskInfo, sizeof( taskInfo )) ;
		proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &proc, PROC_PIDTBSDINFO_SIZE);

		metric_labels_add_lbl2("process_memory", &taskInfo.pti_virtual_size, ALLIGATOR_DATATYPE_INT, 0, "name", proc.pbi_name, "type", "vsz");
		metric_labels_add_lbl2("process_memory", &taskInfo.pti_resident_size, ALLIGATOR_DATATYPE_INT, 0, "name", proc.pbi_name, "type", "rss");
		metric_labels_add_lbl2("process_stats", &taskInfo.pti_threadnum, ALLIGATOR_DATATYPE_INT, 0, "name", proc.pbi_name, "type", "threads");
		metric_labels_add_lbl2("process_stats", &taskInfo.pti_numrunning, ALLIGATOR_DATATYPE_INT, 0, "name", proc.pbi_name, "type", "name");
		metric_labels_add_lbl2("process_stats", &taskInfo.pti_numrunning, ALLIGATOR_DATATYPE_INT, 0, "name", proc.pbi_name, "type", "priority");
		metric_labels_add_lbl2("process_stats", &proc.pbi_status, ALLIGATOR_DATATYPE_INT, 0, "name", proc.pbi_name, "type", "status");
		metric_labels_add_lbl2("process_stats", &proc.pbi_flags, ALLIGATOR_DATATYPE_INT, 0, "name", proc.pbi_name, "type", "flags");
		metric_labels_add_lbl2("process_stats", &proc.pbi_nfiles, ALLIGATOR_DATATYPE_INT, 0, "name", proc.pbi_name, "type", "nfiles");
		metric_labels_add_lbl2("process_stats", &taskInfo.pti_syscalls_unix, ALLIGATOR_DATATYPE_INT, 0, "name", proc.pbi_name, "type", "unix_syscall");
		metric_labels_add_lbl2("process_stats", &taskInfo.pti_syscalls_mach, ALLIGATOR_DATATYPE_INT, 0, "name", proc.pbi_name, "type", "mach_syscall");
		metric_labels_add_lbl2("process_stats", &taskInfo.pti_csw, ALLIGATOR_DATATYPE_INT, 0, "name", proc.pbi_name, "type", "context_switches");
		metric_labels_add_lbl2("process_stats", &taskInfo.pti_faults, ALLIGATOR_DATATYPE_INT, 0, "name", proc.pbi_name, "type", "faults");
		metric_labels_add_lbl2("process_stats", &taskInfo.pti_cow_faults, ALLIGATOR_DATATYPE_INT, 0, "name", proc.pbi_name, "type", "cow_faults");

	}

	return EXIT_SUCCESS ;
}
 


void get_system_metrics()
{
	extern aconf *ac;
	if (ac->system_base)
	{
		get_swap();
		get_cpu();
		get_mem();
	}

	if (ac->system_network)
	{
	}
	if (ac->system_disk)
	{
		get_disk();
	}

	if (ac->system_process)
	{
		get_proc_info();
	}
}
#endif
