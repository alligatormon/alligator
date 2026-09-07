#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include "common/selector.h"
#include "main.h"
#include "metric/labels.h"
#include "common/logs.h"
#include "system/common.h"
#define LINUXFS_LINE_LENGTH 300

extern aconf *ac;

int get_scaling_current_cpu_freq() {
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) {
        carglog(ac->system_carg, L_ERROR, "fopen /proc/cpuinfo: %s\n", strerror(errno));
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
				double hz = mhz * 1000000.0;
                metric_add_labels("cpu_current_frequency_hertz", &hz, DATATYPE_DOUBLE, ac->system_carg, "core", cpu);
            }
        }
    }

    fclose(f);
    return 0;
}

void get_cpu_avg()
{
        double result = ac->system_cpuavg_sum / ac->system_cpuavg_period;
        metric_add_auto("cpu_usage_average_percent", &result, DATATYPE_DOUBLE, ac->system_carg);
}

void cpu_avg_push(double now)
{
	if (now > 100)
		return;

	double old = ac->system_avg_metrics[ac->system_cpuavg_ptr];
	ac->system_avg_metrics[ac->system_cpuavg_ptr] = now;
	ac->system_cpuavg_sum = (ac->system_cpuavg_sum - old) + now;
	carglog(ac->system_carg, L_TRACE, "set ac->system_avg_metrics[%"u64"]=%lf, sum: %lf\n", ac->system_cpuavg_ptr, ac->system_avg_metrics[ac->system_cpuavg_ptr], ac->system_cpuavg_sum);

	++ac->system_cpuavg_ptr;
	if (ac->system_cpuavg_ptr >= ac->system_cpuavg_period)
		ac->system_cpuavg_ptr = 0;
}

void get_cpu(int8_t platform)
{
	int is_cgroup = is_container(platform); // exclude baremetal and virt
	carglog(ac->system_carg, L_TRACE, "fast scrape metrics: base: cpu\n");
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

	char cpu_usage_core_name[30];
	char cpu_usage_time_name[30];

	if (!is_cgroup)
	{
		strlcpy(cpu_usage_core_name, "cpu_usage_core", 15);
		strlcpy(cpu_usage_time_name, "cpu_usage_time", 15);
	}
	else
	{
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
			int64_t t1 = 0, t2 = 0, t3 = 0, t4 = 0, t5 = 0;
			int64_t t6 = 0, t7 = 0, t8 = 0, t9 = 0, t10 = 0;
			char cpuname[16];

			sscanf(temp, "%15s %"d64" %"d64" %"d64" %"d64" %"d64" %"d64" %"d64" %"d64" %"d64" %"d64"",
				cpuname, &t1, &t2, &t3, &t4, &t5, &t6, &t7, &t8, &t9, &t10);
			core_num = atoll(cpuname+3);
			if (!strcmp(cpuname, "cpu"))
				sccs = &ac->scs->hw;
			else
			{
				sccs = &ac->scs->cores[core_num];
				if (core_num >= num_cpus)
				{
					carglog(ac->system_carg, L_ERROR, "error: cpus: %"PRId64", core num: %"PRIu64", field: '%s' (/proc/stat)\n", num_cpus, core_num, temp);
					continue;
				}
			}

			double tuser = t1 / (dividecpu * 100.00);
			double tnice = t2 / (dividecpu * 100.00);
			double tsystem = t3 / (dividecpu * 100.00);
			double tidle = t4 / (dividecpu * 100.00);
			double tiowait = t5 / (dividecpu * 100.00);
			double tirq = t6 / (dividecpu * 100.00);
			double tsoftirq = t7 / (dividecpu * 100.00);
			double tsteal = t8 / (dividecpu * 100.00);
			double tguest = (t9 + t10) / (dividecpu * 100.00);

			uint64_t tsum = (uint64_t)(t1 + t2 + t3 + t4 + t5 + t6 + t7 + t8 + t9 + t10);
			uint64_t tdelta = tsum - sccs->total;
			sccs->total = tsum;
			if (!tdelta)
				continue;

			int64_t du_user = t1 - (int64_t)sccs->user;
			sccs->user = t1;
			int64_t du_nice = t2 - (int64_t)sccs->nice;
			sccs->nice = t2;
			int64_t du_system = t3 - (int64_t)sccs->system;
			sccs->system = t3;
			int64_t du_idle = t4 - (int64_t)sccs->idle;
			sccs->idle = t4;
			int64_t du_iowait = t5 - (int64_t)sccs->iowait;
			sccs->iowait = t5;
			int64_t du_irq = t6 - (int64_t)sccs->irq;
			sccs->irq = t6;
			int64_t du_softirq = t7 - (int64_t)sccs->softirq;
			sccs->softirq = t7;
			int64_t du_steal = t8 - (int64_t)sccs->steal;
			sccs->steal = t8;
			int64_t du_guest = (t9 + t10) - (int64_t)(sccs->guest + sccs->guest_nice);
			sccs->guest = t9;
			sccs->guest_nice = t10;

			double usage = ((du_user + du_system)*100.0/tdelta);
			if (usage<0)
				usage = 0;
			double user = ((du_user)*100.0/tdelta);
			if (user<0)
				user = 0;
			double nice = ((du_nice)*100.0/tdelta);
			if (nice<0)
				nice = 0;
			double system = ((du_system)*100.0/tdelta);
			if (system<0)
				system = 0;
			double idle = ((du_idle)*100.0/tdelta);
			if (idle<0)
				idle = 0;
			double iowait = ((du_iowait)*100.0/tdelta);
			if (iowait<0)
				iowait = 0;
			double irq = ((du_irq)*100.0/tdelta);
			if (irq<0)
				irq = 0;
			double softirq = ((du_softirq)*100.0/tdelta);
			if (softirq<0)
				softirq = 0;
			double steal = ((du_steal)*100.0/tdelta);
			if (steal<0)
				steal = 0;
			double guest = ((du_guest)*100.0/tdelta);
			if (guest<0)
				guest = 0;

			if (!strcmp(cpuname, "cpu"))
			{
				metric_add_labels(cpu_usage_time_name, &tuser, DATATYPE_DOUBLE, ac->system_carg, "type", "user");
				metric_add_labels(cpu_usage_time_name, &tnice, DATATYPE_DOUBLE, ac->system_carg, "type", "nice");
				metric_add_labels(cpu_usage_time_name, &tsystem, DATATYPE_DOUBLE, ac->system_carg, "type", "system");
				metric_add_labels(cpu_usage_time_name, &tidle, DATATYPE_DOUBLE, ac->system_carg, "type", "idle");
				metric_add_labels(cpu_usage_time_name, &tiowait, DATATYPE_DOUBLE, ac->system_carg, "type", "iowait");
				metric_add_labels(cpu_usage_time_name, &tirq, DATATYPE_DOUBLE, ac->system_carg, "type", "irq");
				metric_add_labels(cpu_usage_time_name, &tsoftirq, DATATYPE_DOUBLE, ac->system_carg, "type", "softirq");
				metric_add_labels(cpu_usage_time_name, &tsteal, DATATYPE_DOUBLE, ac->system_carg, "type", "steal");
				metric_add_labels(cpu_usage_time_name, &tguest, DATATYPE_DOUBLE, ac->system_carg, "type", "guest");
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
				metric_add_labels2(cpu_usage_core_name, &irq, DATATYPE_DOUBLE, ac->system_carg, "type", "irq", "cpu", cpuname);
				metric_add_labels2(cpu_usage_core_name, &softirq, DATATYPE_DOUBLE, ac->system_carg, "type", "softirq", "cpu", cpuname);
				metric_add_labels2(cpu_usage_core_name, &steal, DATATYPE_DOUBLE, ac->system_carg, "type", "steal", "cpu", cpuname);
				metric_add_labels2(cpu_usage_core_name, &guest, DATATYPE_DOUBLE, ac->system_carg, "type", "guest", "cpu", cpuname);
			}
		}
		else if ( !strncmp(temp, "processes", 9) )
		{
			int64_t spacenum = strcspn(temp, " ");
			spacenum += strcspn(temp+spacenum, " ");
			uint64_t ival = atoll(temp+spacenum);
			metric_add_auto("forks_total", &ival, DATATYPE_UINT, ac->system_carg);
		}
		else if ( !strncmp(temp, "ctxt", 4) )
		{
			int64_t spacenum = strcspn(temp, " ");
			spacenum += strcspn(temp+spacenum, " ");
			uint64_t ival = atoll(temp+spacenum);
			metric_add_auto("context_switches_total", &ival, DATATYPE_UINT, ac->system_carg);
		}
		else if ( !strncmp(temp, "intr", 4) )
		{
			int64_t spacenum = strcspn(temp, " ");
			spacenum += strcspn(temp+spacenum, " ");
			uint64_t ival = atoll(temp+spacenum);
			metric_add_auto("interrupts_total", &ival, DATATYPE_UINT, ac->system_carg);
		}
		else if ( !strncmp(temp, "softirq", 7) )
		{
			int64_t spacenum = strcspn(temp, " ");
			spacenum += strcspn(temp+spacenum, " ");
			uint64_t ival = atoll(temp+spacenum);
			metric_add_auto("softirq_total", &ival, DATATYPE_UINT, ac->system_carg);
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
			carglog(ac->system_carg, L_ERROR, "Not found %s\n", syspath);
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
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: get_cpu time execute '%"d64", diff '%lf'\n", scrape_time, diff_time);

	ac->last_time_cpu = ts_start;
	metric_add_auto("cpu_usage_calc_delta_seconds", &diff_time, DATATYPE_DOUBLE, ac->system_carg);

	uint64_t sec = ts_end.sec;
	metric_add_auto("time_now", &sec, DATATYPE_UINT, ac->system_carg);
}

#ifdef __linux__

static int cpuidle_is_state_dir(const char *name)
{
	size_t i;

	if (!name || strncmp(name, "state", 5) || !name[5])
		return 0;
	for (i = 5; name[i]; ++i) {
		if (!isdigit((unsigned char)name[i]))
			return 0;
	}
	return 1;
}

static int cpuidle_read_u64(const char *path, uint64_t *out)
{
	FILE *fd;
	char buf[64];
	char *end = NULL;
	unsigned long long v;

	fd = fopen(path, "r");
	if (!fd)
		return 0;
	if (!fgets(buf, sizeof(buf), fd)) {
		fclose(fd);
		return 0;
	}
	fclose(fd);

	errno = 0;
	v = strtoull(buf, &end, 10);
	if (end == buf || errno == ERANGE)
		return 0;
	*out = (uint64_t)v;
	return 1;
}

static void cpuidle_read_state(const char *statedir, const char *cpu, const char *statedirname)
{
	char path[1024];
	char state[64];
	uint64_t val;

	snprintf(path, sizeof(path), "%s/name", statedir);
	if (!getkvfile_str(path, state, sizeof(state)) || !state[0])
		strlcpy(state, statedirname, sizeof(state));

	snprintf(path, sizeof(path), "%s/time", statedir);
	if (cpuidle_read_u64(path, &val)) {
		double sec = (double)val / 1000000.0;
		metric_add_labels2("cpu_cstate_seconds_total", &sec, DATATYPE_DOUBLE, ac->system_carg,
			"cpu", (char *)cpu, "state", state);
	}

	snprintf(path, sizeof(path), "%s/usage", statedir);
	if (cpuidle_read_u64(path, &val)) {
		metric_add_labels2("cpu_cstate_usage_total", &val, DATATYPE_UINT, ac->system_carg,
			"cpu", (char *)cpu, "state", state);
	}

	snprintf(path, sizeof(path), "%s/disable", statedir);
	if (cpuidle_read_u64(path, &val)) {
		metric_add_labels2("cpu_cstate_disabled", &val, DATATYPE_UINT, ac->system_carg,
			"cpu", (char *)cpu, "state", state);
	}
}

static void cpuidle_read_cpu(const char *cpudir, const char *cpuname)
{
	char idledir[768];
	DIR *dp;
	struct dirent *ent;
	char cpu[16];

	strlcpy(cpu, cpuname + 3, sizeof(cpu));
	snprintf(idledir, sizeof(idledir), "%s/%s/cpuidle", cpudir, cpuname);
	dp = opendir(idledir);
	if (!dp)
		return;

	while ((ent = readdir(dp))) {
		char statedir[1024];

		if (!cpuidle_is_state_dir(ent->d_name))
			continue;
		snprintf(statedir, sizeof(statedir), "%s/%s", idledir, ent->d_name);
		cpuidle_read_state(statedir, cpu, ent->d_name);
	}
	closedir(dp);
}

void get_linux_cpuidle(void)
{
	char cpudir[512];
	char fpath[768];
	char name[64];
	DIR *dp;
	struct dirent *ent;
	uint64_t one = 1;

	if (!ac || !ac->system_sysfs || !ac->system_carg)
		return;

	snprintf(cpudir, sizeof(cpudir), "%s/devices/system/cpu", ac->system_sysfs);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: cpuidle '%s'\n", cpudir);

	snprintf(fpath, sizeof(fpath), "%s/cpuidle/current_driver", cpudir);
	if (getkvfile_str(fpath, name, sizeof(name)) && name[0])
		metric_add_labels("cpu_cstate_driver", &one, DATATYPE_UINT, ac->system_carg, "driver", name);

	snprintf(fpath, sizeof(fpath), "%s/cpuidle/current_governor", cpudir);
	if (getkvfile_str(fpath, name, sizeof(name)) && name[0])
		metric_add_labels("cpu_cstate_governor", &one, DATATYPE_UINT, ac->system_carg, "governor", name);

	dp = opendir(cpudir);
	if (!dp)
		return;

	while ((ent = readdir(dp))) {
		if (strncmp(ent->d_name, "cpu", 3) || !isdigit((unsigned char)ent->d_name[3]))
			continue;
		cpuidle_read_cpu(cpudir, ent->d_name);
	}
	closedir(dp);
}

#endif