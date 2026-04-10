#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "main.h"
#include "metric/labels.h"
#include "common/logs.h"
#include "system/common.h"
#define LINUXFS_LINE_LENGTH 300

extern aconf *ac;

int get_scaling_current_cpu_freq() {
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) {
        perror("fopen");
        return 1;
    }

    char line[LINUXFS_LINE_LENGTH];
    char cpu[10] = {0};
    double mhz = 0.0;

    while (fgets(line, sizeof(line), f)) {

        if (strncmp(line, "processor", 9) == 0) {
			sscanf(line, "processor : %9s", cpu);
        }

        if (strncmp(line, "cpu MHz", 7) == 0) {
            sscanf(line, "cpu MHz : %lf", &mhz);
			if (cpu[0]) {
                metric_add_labels("cpu_current_frequency", &mhz, DATATYPE_DOUBLE, ac->system_carg, "core", cpu);
            }
        }
    }

    fclose(f);
    return 0;
}

void get_cpu_avg()
{
        double result = ac->system_cpuavg_sum / ac->system_cpuavg_period;
        metric_add_auto("cpu_avg", &result, DATATYPE_DOUBLE, ac->system_carg);
}

void cpu_avg_push(double now)
{
	if (now > 100)
		return;

	double old = ac->system_avg_metrics[ac->system_cpuavg_ptr];
	ac->system_avg_metrics[ac->system_cpuavg_ptr] = now;
	ac->system_cpuavg_sum = (ac->system_cpuavg_sum - old) + now;
	carglog(ac->system_carg, L_INFO, "set ac->system_avg_metrics[%"u64"]=%lf, sum: %lf\n", ac->system_cpuavg_ptr, ac->system_avg_metrics[ac->system_cpuavg_ptr], ac->system_cpuavg_sum);

	++ac->system_cpuavg_ptr;
	if (ac->system_cpuavg_ptr >= ac->system_cpuavg_period)
		ac->system_cpuavg_ptr = 0;
}

void get_cpu(int8_t platform)
{
	int is_cgroup = is_container(platform); // exclude baremetal and virt
	carglog(ac->system_carg, L_INFO, "fast scrape metrics: base: cpu\n");
	r_time ts_start = setrtime();

	int64_t effective_cores;
	int64_t num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	char syspath[255];
	snprintf(syspath, 255, "%s/fs/cgroup/cpu/cpu.cfs_quota_us", ac->system_sysfs);
	int64_t num_cpus_cgroup = (getkvfile(syspath)/100000);
	double dividecpu = 1;
	if ( num_cpus_cgroup <= 0 )
		effective_cores = dividecpu = num_cpus;
	else if ( num_cpus < num_cpus_cgroup )
	{
		effective_cores = dividecpu = num_cpus;
	}
	else
		effective_cores = dividecpu = num_cpus_cgroup;

	if (!ac->scs->cores)
		ac->scs->cores = calloc(1, sizeof(system_cpu_cores_stats)*num_cpus);


	metric_add_auto("cores_num_hw", &num_cpus, DATATYPE_INT, ac->system_carg);
	metric_add_auto("cores_num_cgroup", &num_cpus_cgroup, DATATYPE_INT, ac->system_carg);
	metric_add_auto("cores_num", &effective_cores, DATATYPE_INT, ac->system_carg);

	char temp[LINUXFS_LINE_LENGTH];
	uint64_t core_num = 0;
	system_cpu_cores_stats *sccs;

	char cpu_usage_name[30];
	char cpu_usage_core_name[30];
	char cpu_usage_time_name[30];

	if (!is_cgroup)
	{
		strlcpy(cpu_usage_name, "cpu_usage", 10);
		strlcpy(cpu_usage_core_name, "cpu_usage_core", 15);
		strlcpy(cpu_usage_time_name, "cpu_usage_time", 15);
	}
	else
	{
		strlcpy(cpu_usage_name, "cpu_usage_hw", 13);
		strlcpy(cpu_usage_core_name, "cpu_usage_hw_core", 18);
		strlcpy(cpu_usage_time_name, "cpu_usage_hw_time", 18);
	}

	// hw stats
	char procstat[255];
	snprintf(procstat, 255, "%s/stat", ac->system_procfs);
	FILE *fd = fopen(procstat, "r");
	if ( !fd )
		return;

	while ( fgets(temp, LINUXFS_LINE_LENGTH, fd) )
	{
		if ( !strncmp(temp, "cpu", 3) )
		{
			int64_t t1, t2, t3, t4, t5;
			char cpuname[6];

			sscanf(temp, "%5s %"d64" %"d64" %"d64" %"d64" %"d64"", cpuname, &t1, &t2, &t3, &t4, &t5);
			core_num = atoll(cpuname+3);
			if (!strcmp(cpuname, "cpu"))
				sccs = &ac->scs->hw;
			else
			{
				sccs = &ac->scs->cores[core_num];
				if (core_num >= num_cpus)
				{
					printf("error: cpus: %"PRId64", core num: %"PRIu64", field: '%s' (/proc/stat)\n", num_cpus, core_num, temp);
					continue;
				}
			}

			double tuser = t1 / (dividecpu * 100.00);
			double tnice = t2 / (dividecpu * 100.00);
			double tsystem = t3 / (dividecpu * 100.00);
			double tidle = t4 / (dividecpu * 100.00);
			double tiowait = t5 / (dividecpu * 100.00);

			uint64_t t12345 = t1+t2+t3+t4+t5 - sccs->total;
			sccs->total += t12345;
			if (!t12345)
				continue;

			t1 -= sccs->user;
			sccs->user += t1;
			t2 -= sccs->nice;
			sccs->nice += t2;
			t3 -= sccs->system;
			sccs->system += t3;
			t4 -= sccs->idle;
			sccs->idle += t4;
			t5 -= sccs->iowait;
			sccs->iowait += t5;

			double usage = ((t1 + t3)*100.0/t12345);
			if (usage<0)
				usage = 0;
			double user = ((t1)*100.0/t12345);
			if (user<0)
				user = 0;
			double nice = ((t2)*100.0/t12345);
			if (nice<0)
				nice = 0;
			double system = ((t3)*100.0/t12345);
			if (system<0)
				system = 0;
			double idle = ((t4)*100.0/t12345);
			if (idle<0)
				idle = 0;
			double iowait = ((t5)*100.0/t12345);
			if (iowait<0)
				iowait = 0;

			if (!strcmp(cpuname, "cpu"))
			{
				metric_add_labels(cpu_usage_name, &user, DATATYPE_DOUBLE, ac->system_carg, "type", "user");
				metric_add_labels(cpu_usage_name, &system, DATATYPE_DOUBLE, ac->system_carg, "type", "system");
				metric_add_labels(cpu_usage_name, &nice, DATATYPE_DOUBLE, ac->system_carg, "type", "nice");
				metric_add_labels(cpu_usage_name, &idle, DATATYPE_DOUBLE, ac->system_carg, "type", "idle");
				metric_add_labels(cpu_usage_name, &iowait, DATATYPE_DOUBLE, ac->system_carg, "type", "iowait");
				metric_add_labels(cpu_usage_time_name, &tuser, DATATYPE_DOUBLE, ac->system_carg, "type", "user");
				metric_add_labels(cpu_usage_time_name, &tnice, DATATYPE_DOUBLE, ac->system_carg, "type", "nice");
				metric_add_labels(cpu_usage_time_name, &tsystem, DATATYPE_DOUBLE, ac->system_carg, "type", "system");
				metric_add_labels(cpu_usage_time_name, &tidle, DATATYPE_DOUBLE, ac->system_carg, "type", "idle");
				metric_add_labels(cpu_usage_time_name, &tiowait, DATATYPE_DOUBLE, ac->system_carg, "type", "iowait");
				if (!is_cgroup && ac->system_cpuavg)
				{
					cpu_avg_push(usage);
				}
			}
			else
			{
				metric_add_labels2(cpu_usage_core_name, &user, DATATYPE_DOUBLE, ac->system_carg, "type", "user", "cpu", cpuname);
				metric_add_labels2(cpu_usage_core_name, &system, DATATYPE_DOUBLE, ac->system_carg, "type", "system", "cpu", cpuname);
				metric_add_labels2(cpu_usage_core_name, &nice, DATATYPE_DOUBLE, ac->system_carg, "type", "nice", "cpu", cpuname);
				metric_add_labels2(cpu_usage_core_name, &idle, DATATYPE_DOUBLE, ac->system_carg, "type", "idle", "cpu", cpuname);
				metric_add_labels2(cpu_usage_core_name, &iowait, DATATYPE_DOUBLE, ac->system_carg, "type", "iowait", "cpu", cpuname);
			}
		}
		else if ( !strncmp(temp, "processes", 9) )
		{
			int64_t spacenum = strcspn(temp, " ");
			spacenum += strcspn(temp+spacenum, " ");
			uint64_t ival = atoll(temp+spacenum);
			metric_add_auto("forks", &ival, DATATYPE_UINT, ac->system_carg);
		}
		else if ( !strncmp(temp, "ctxt", 4) )
		{
			int64_t spacenum = strcspn(temp, " ");
			spacenum += strcspn(temp+spacenum, " ");
			uint64_t ival = atoll(temp+spacenum);
			metric_add_auto("context_switches", &ival, DATATYPE_UINT, ac->system_carg);
		}
		else if ( !strncmp(temp, "intr", 4) )
		{
			int64_t spacenum = strcspn(temp, " ");
			spacenum += strcspn(temp+spacenum, " ");
			uint64_t ival = atoll(temp+spacenum);
			metric_add_auto("interrupts", &ival, DATATYPE_UINT, ac->system_carg);
		}
		else if ( !strncmp(temp, "softirq", 7) )
		{
			int64_t spacenum = strcspn(temp, " ");
			spacenum += strcspn(temp+spacenum, " ");
			uint64_t ival = atoll(temp+spacenum);
			metric_add_auto("softirq", &ival, DATATYPE_UINT, ac->system_carg);
		}
	}
	fclose(fd);

	// cgroup stats
	if (is_cgroup)
	{
		sccs = &ac->scs->cgroup;

		snprintf(syspath, 255, "%s/fs/cgroup/cpu/cpu.cfs_period_us", ac->system_sysfs);
		int64_t cfs_period = getkvfile(syspath);
		if (cfs_period <= 0)
			cfs_period = 100000;

		snprintf(syspath, 255, "%s/fs/cgroup/cpu/cpu.cfs_quota_us", ac->system_sysfs);
		int64_t cfs_quota = getkvfile(syspath);
		if ( cfs_quota < 0 )
		{
			cfs_quota = cfs_period*dividecpu;
		}
		else if (!cfs_quota)
		{
			cfs_quota = cfs_period;
		}

		snprintf(syspath, 255, "%s/fs/cgroup/cpuacct/cpuacct.stat", ac->system_sysfs);
		FILE *fd = fopen(syspath, "r");
		if (!fd)
		{
			printf("Not found %s\n", syspath);
			return;
		}

		char *tmp;
		char buf[1000];
		int64_t cgroup_system_ticks = 0;
		int64_t cgroup_user_ticks = 0;
		while (fgets(buf, 1000, fd))
		{
			if (!strncmp(buf, "user", 4))
			{
				tmp = buf;
				tmp += strcspn(buf, " \t");
				tmp += strspn(buf, " \t");
				cgroup_user_ticks = strtoll(tmp, NULL, 10);
			}
			else if (!strncmp(buf, "system", 6))
			{
				tmp = buf;
				tmp += strcspn(buf, " \t");
				tmp += strspn(buf, " \t");
				cgroup_system_ticks = strtoll(tmp, NULL, 10);
			}
		}

		fclose(fd);

		cgroup_system_ticks -= sccs->system;
		sccs->system += cgroup_system_ticks;

		cgroup_user_ticks -= sccs->user;
		sccs->user += cgroup_user_ticks;

		dividecpu = (cfs_quota*1.0/cfs_period);
		if (dividecpu <= 0)
			dividecpu = 1;
		double cgroup_system_usage = cgroup_system_ticks*1.0/dividecpu;
		double cgroup_user_usage = cgroup_user_ticks*1.0/dividecpu;
		double cgroup_total_usage = cgroup_system_usage + cgroup_user_usage;
		if (!cgroup_system_usage && !cgroup_user_usage)
			cgroup_total_usage = 0;
		else if (!cgroup_total_usage)
		{
			snprintf(syspath, 255, "%s/fs/cgroup/cpuacct/cpuacct.usage", ac->system_sysfs);
			cgroup_total_usage = getkvfile(syspath)*cfs_period/1000000000/cfs_quota*100.0;
		}

		double time_system = sccs->system / (dividecpu * 100.0);
		double time_user = sccs->user / (dividecpu * 100.0);

		carglog(ac->system_carg, L_DEBUG, "cgroup CPU: user %lf, system %lf\n", cgroup_user_usage, cgroup_system_usage);
		metric_add_labels("cpu_usage", &cgroup_user_usage, DATATYPE_DOUBLE, ac->system_carg, "type", "user");
		metric_add_labels("cpu_usage", &cgroup_system_usage, DATATYPE_DOUBLE, ac->system_carg, "type", "system");
		metric_add_labels("cpu_usage_time", &time_system, DATATYPE_DOUBLE, ac->system_carg, "type", "system");
		metric_add_labels("cpu_usage_time", &time_user, DATATYPE_DOUBLE, ac->system_carg, "type", "user");

		if (ac->system_cpuavg)
		{
			cpu_avg_push(cgroup_total_usage);
		}
	}
	r_time ts_end = setrtime();
	int64_t scrape_time = getrtime_ns(ts_start, ts_end);
	double diff_time = getrtime_ns(ac->last_time_cpu, ts_start)/1000000000.0;
	carglog(ac->system_carg, L_INFO, "system scrape metrics: base: get_cpu time execute '%"d64", diff '%lf'\n", scrape_time, diff_time);

	ac->last_time_cpu = ts_start;
	metric_add_auto("cpu_usage_calc_delta_time", &diff_time, DATATYPE_DOUBLE, ac->system_carg);

	uint64_t sec = ts_end.sec;
	metric_add_auto("time_now", &sec, DATATYPE_UINT, ac->system_carg);
}