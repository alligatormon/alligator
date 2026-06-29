#ifdef __FreeBSD__
#include "main.h"
#include "system/common.h"
#include "common/selector.h"
#include "common/logs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <kvm.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <sys/rctl.h>
#include <utmp.h>

extern aconf *ac;

#define INTRNAME_LEN 20

static void emit_platform_label(const char *platform, int8_t mode)
{
	int64_t vl = 1;

	if (!mode)
		return;
	metric_add_labels("server_platform", &vl, DATATYPE_INT, ac->system_carg, "platform", platform);
}

int8_t get_platform(int8_t mode)
{
	char guest[32];
	int jailed = 0;
	size_t sz;

	if (ac->system_platform == PLATFORM_OPENSTACK) {
		emit_platform_label("openstack", mode);
		return PLATFORM_OPENSTACK;
	}
	if (ac->system_platform == PLATFORM_KVM) {
		emit_platform_label("kvm", mode);
		return PLATFORM_KVM;
	}

	sz = sizeof(jailed);
	if (sysctlbyname("security.jail.jailed", &jailed, &sz, NULL, 0) == 0 && jailed) {
		emit_platform_label("jail", mode);
		return PLATFORM_LXC;
	}

	sz = sizeof(guest);
	if (sysctlbyname("kern.vm_guest", guest, &sz, NULL, 0) == 0) {
		if (!strcmp(guest, "bhyve")) {
			emit_platform_label("kvm", mode);
			return PLATFORM_KVM;
		}
		if (strcmp(guest, "none") && guest[0]) {
			emit_platform_label(guest, mode);
			return PLATFORM_KVM;
		}
	}

	emit_platform_label("bare-metal", mode);
	return PLATFORM_BAREMETAL;
}

void get_kernel_version(int8_t platform)
{
	struct utsname un;
	const char *platform_name = "unknown";
	int64_t vl = 1;
	char *p;
	uint8_t i;

	if (uname(&un) != 0)
		return;

	switch (platform) {
	case PLATFORM_BAREMETAL:
		platform_name = "bare-metal";
		break;
	case PLATFORM_LXC:
		platform_name = "jail";
		break;
	case PLATFORM_DOCKER:
		platform_name = "docker";
		break;
	case PLATFORM_KVM:
		platform_name = "kvm";
		break;
	case PLATFORM_OPENSTACK:
		platform_name = "openstack";
		break;
	default:
		break;
	}

	metric_add_labels2("kernel_version", &vl, DATATYPE_INT, ac->system_carg,
		"version", un.release, "platform", platform_name);

	i = 0;
	p = un.release;
	while (*p && i < sizeof(ac->kernel_version)) {
		if (isdigit((unsigned char)*p)) {
			ac->kernel_version[i] = (uint8_t)strtol(p, &p, 10);
			i++;
		} else {
			p++;
		}
	}
}

void get_distribution_name(void)
{
	char fname[255];
	char buf[255];
	char *ptr;
	char name[255] = "FreeBSD";
	char version[255] = "";
	char pretty_name[255] = "";
	FILE *fd;

	snprintf(fname, sizeof(fname), "%s/os-release", ac->system_etcdir);
	fd = fopen(fname, "r");
	if (!fd) {
		FILE *fv = popen("freebsd-version", "r");
		if (fv) {
			if (fgets(version, sizeof(version), fv)) {
				version[strcspn(version, "\r\n")] = 0;
				snprintf(pretty_name, sizeof(pretty_name), "FreeBSD %s", version);
			}
			pclose(fv);
		}
	} else {
		while (fgets(buf, sizeof(buf), fd)) {
			if (!strncmp(buf, "NAME", 4)) {
				ptr = buf + 4 + strcspn(buf + 4, "=\"");
				ptr += strspn(ptr, "=\"");
				ptr[strcspn(ptr, "\r\n\"")] = 0;
				strlcpy(name, ptr, sizeof(name));
			} else if (!strncmp(buf, "VERSION_ID", 10)) {
				ptr = buf + 10 + strcspn(buf + 10, "=\"");
				ptr += strspn(ptr, "=\"");
				ptr[strcspn(ptr, "\r\n\"")] = 0;
				strlcpy(version, ptr, sizeof(version));
			} else if (!strncmp(buf, "VERSION", 7) && version[0] == 0) {
				ptr = buf + 7 + strcspn(buf + 7, "=\"");
				ptr += strspn(ptr, "=\"");
				ptr[strcspn(ptr, "\r\n\"")] = 0;
				strlcpy(version, ptr, sizeof(version));
			} else if (!strncmp(buf, "PRETTY_NAME", 11)) {
				ptr = buf + 11 + strspn(buf + 11, "=\"");
				ptr += strspn(ptr, "=\"");
				ptr[strcspn(ptr, "\r\n\"")] = 0;
				strlcpy(pretty_name, ptr, sizeof(pretty_name));
			}
		}
		fclose(fd);
	}

	if (!pretty_name[0] && version[0])
		snprintf(pretty_name, sizeof(pretty_name), "%s %s", name, version);

	{
		uint64_t okval = 1;
		metric_add_labels4("os_distribution", &okval, DATATYPE_UINT, ac->system_carg,
			"os", "freebsd", "name", name, "version", version, "pretty", pretty_name);
	}
}

static void utmp_parse(struct utmp *log)
{
	if (!log || log->ut_type != USER_PROCESS)
		return;

	{
		int64_t time = log->ut_tv.tv_sec;
		if (!strncmp(log->ut_line, "tty", 3)) {
			metric_add_labels4("utmp_logged_in_timestamp_seconds", &time, DATATYPE_INT, ac->system_carg,
				"user", log->ut_user, "host", log->ut_host, "type", "terminal", "terminal", log->ut_line);
		}
		if (!strncmp(log->ut_line, "pts", 3) || !strncmp(log->ut_line, "p", 1)) {
			metric_add_labels4("utmp_logged_in_timestamp_seconds", &time, DATATYPE_INT, ac->system_carg,
				"user", log->ut_user, "host", log->ut_host, "type", "pseudo-terminal", "terminal", log->ut_line);
		}
	}
}

void get_utmp_info(void)
{
	FILE *file;
	struct utmp log;
	uint64_t btmp_size = get_file_size("/var/log/btmp");

	metric_add_auto("btmp_file_size", &btmp_size, DATATYPE_UINT, ac->system_carg);

	file = fopen("/var/run/utmp", "rb");
	if (!file)
		return;

	while (fread(&log, sizeof(log), 1, file) == 1)
		utmp_parse(&log);

	fclose(file);
}

void get_memory_usage_hw(void)
{
	u_int page_size;
	struct vmtotal vmt;
	size_t sz;
	int64_t physmem;
	int64_t v_active_count;
	int64_t v_inactive_count;
	u_int wire_count;
	int64_t free_memory, avail_memory, active_memory, inact_memory, wire_memory, usage_memory;
	int64_t swap_total = 0, swap_used = 0, swap_free = 0;
	struct kvm_swap swap;
	kvm_t *kd;

	sz = sizeof(vmt);
	if (sysctlbyname("vm.vmtotal", &vmt, &sz, NULL, 0) != 0)
		return;

	sz = sizeof(page_size);
	if (sysctlbyname("vm.stats.vm.v_page_size", &page_size, &sz, NULL, 0) != 0)
		return;

	sz = sizeof(physmem);
	if (sysctlbyname("hw.physmem", &physmem, &sz, NULL, 0) != 0)
		return;

	sz = sizeof(v_active_count);
	sysctlbyname("vm.stats.vm.v_active_count", &v_active_count, &sz, NULL, 0);
	sz = sizeof(v_inactive_count);
	sysctlbyname("vm.stats.vm.v_inactive_count", &v_inactive_count, &sz, NULL, 0);
	sz = sizeof(wire_count);
	sysctlbyname("vm.stats.vm.v_wire_count", &wire_count, &sz, NULL, 0);

	free_memory = vmt.t_free * (int64_t)page_size;
	avail_memory = vmt.t_avm * (int64_t)page_size;
	active_memory = v_active_count * (int64_t)page_size;
	inact_memory = v_inactive_count * (int64_t)page_size;
	wire_memory = wire_count * (int64_t)page_size;
	usage_memory = physmem - avail_memory;

	metric_add_labels("memory_usage_hw", &physmem, DATATYPE_INT, ac->system_carg, "type", "total");
	metric_add_labels("memory_usage_hw", &avail_memory, DATATYPE_INT, ac->system_carg, "type", "available");
	metric_add_labels("memory_usage_hw", &active_memory, DATATYPE_INT, ac->system_carg, "type", "active");
	metric_add_labels("memory_usage_hw", &inact_memory, DATATYPE_INT, ac->system_carg, "type", "inactive");
	metric_add_labels("memory_usage_hw", &free_memory, DATATYPE_INT, ac->system_carg, "type", "free");
	metric_add_labels("memory_usage_hw", &wire_memory, DATATYPE_INT, ac->system_carg, "type", "wire");
	metric_add_labels("memory_usage_hw", &usage_memory, DATATYPE_INT, ac->system_carg, "type", "usage");

	kd = kvm_open(NULL, NULL, NULL, O_RDONLY, "kvm_open");
	if (kd) {
		if (kvm_getswapinfo(kd, &swap, 1, 0) == 0) {
			swap_total = swap.ksw_total * (int64_t)getpagesize();
			swap_used = swap.ksw_used * (int64_t)getpagesize();
			swap_free = swap_total - swap_used;
			metric_add_labels("memory_usage_hw", &swap_total, DATATYPE_INT, ac->system_carg, "type", "swap_total");
			metric_add_labels("memory_usage_hw", &swap_free, DATATYPE_INT, ac->system_carg, "type", "swap_free");
			metric_add_labels("memory_usage_hw", &swap_used, DATATYPE_INT, ac->system_carg, "type", "swap_usage");
		}
		kvm_close(kd);
	}
}

void get_memory_usage_cgroup(void)
{
	int jailed = 0;
	size_t sz = sizeof(jailed);
	char query[64];
	char buf[4096];
	char *tmp;
	int64_t val;

	if (sysctlbyname("security.jail.jailed", &jailed, &sz, NULL, 0) != 0 || !jailed)
		return;

	snprintf(query, sizeof(query), "jail:0:memoryuse");
	if (rctl_get_racct(query, strlen(query) + 1, buf, sizeof(buf)) != 0)
		return;

	tmp = strstr(buf, "memoryuse=");
	if (!tmp)
		return;
	tmp += strlen("memoryuse=");
	val = strtoll(tmp, NULL, 10);
	metric_add_labels("memory_usage_cgroup", &val, DATATYPE_INT, ac->system_carg, "type", "usage");

	tmp = strstr(buf, "vmemoryuse=");
	if (tmp) {
		tmp += strlen("vmemoryuse=");
		val = strtoll(tmp, NULL, 10);
		metric_add_labels("memory_usage_cgroup", &val, DATATYPE_INT, ac->system_carg, "type", "total");
	}
}

void get_proc_interrupts(int extended_mode)
{
	int ncpu = 0;
	size_t names_sz = 0, cnt_sz = 0, sz;
	char *names = NULL;
	uint64_t *cnt = NULL;
	int nintr;
	int i, cpu;

	sz = sizeof(ncpu);
	if (sysctlbyname("hw.ncpu", &ncpu, &sz, NULL, 0) != 0 || ncpu < 1)
		return;

	if (sysctlbyname("hw.intrnames", NULL, &names_sz, NULL, 0) != 0)
		return;
	if (sysctlbyname("hw.intrcnt", NULL, &cnt_sz, NULL, 0) != 0)
		return;
	if (!names_sz || !cnt_sz)
		return;

	nintr = (int)(cnt_sz / (sizeof(uint64_t) * (size_t)ncpu));
	if (!nintr)
		return;

	names = malloc(names_sz);
	cnt = malloc(cnt_sz);
	if (!names || !cnt)
		goto out;

	if (sysctlbyname("hw.intrnames", names, &names_sz, NULL, 0) != 0)
		goto out;
	if (sysctlbyname("hw.intrcnt", cnt, &cnt_sz, NULL, 0) != 0)
		goto out;

	for (i = 0; i < nintr; i++) {
		char code[16];
		char description[INTRNAME_LEN + 1];
		uint64_t total = 0;

		snprintf(code, sizeof(code), "%d", i);
		strlcpy(description, names + i * INTRNAME_LEN, sizeof(description));
		normalize_spaces(description);

		for (cpu = 0; cpu < ncpu; cpu++) {
			uint64_t value = cnt[i * ncpu + cpu];
			total += value;
			if (extended_mode) {
				char cpuname[16];
				snprintf(cpuname, sizeof(cpuname), "CPU%d", cpu);
				metric_add_labels3("interrupts_core_count", &value, DATATYPE_UINT, ac->system_carg,
					"code", code, "description", description, "cpu", cpuname);
			}
		}

		metric_add_labels2("interrupts_by_irq_total", &total, DATATYPE_UINT, ac->system_carg,
			"code", code, "description", description);
	}

out:
	free(names);
	free(cnt);
}

void get_thermal(void)
{
	int cpu;

	for (cpu = 0; ; cpu++) {
		char sysctl_name[32];
		char tempbuf[16];
		size_t sz = sizeof(tempbuf);
		char core[16];
		int64_t temp_c = 0;
		char *end;

		snprintf(sysctl_name, sizeof(sysctl_name), "dev.cpu.%d.temperature", cpu);
		if (sysctlbyname(sysctl_name, tempbuf, &sz, NULL, 0) != 0)
			break;

		tempbuf[sz ? sz - 1 : 0] = 0;
		temp_c = (int64_t)strtod(tempbuf, &end);

		snprintf(core, sizeof(core), "%d", cpu);
		metric_add_labels3("core_temperature_celsius", &temp_c, DATATYPE_INT, ac->system_carg,
			"name", "cpu", "component", core, "hwmon", "dev.cpu");
	}
}
#endif
