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
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>
#include <sys/utsname.h>
#include <sys/rctl.h>
#include <utmpx.h>

extern aconf *ac;

#define INTRNAME_LEN 20

static void emit_platform_label(char *platform, int8_t mode)
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
	char *platform_name = "unknown";
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

static void utmp_parse(struct utmpx *log)
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
	struct utmpx *log;
	uint64_t btmp_size = get_file_size("/var/log/btmp");

	metric_add_auto("btmp_file_size", &btmp_size, DATATYPE_UINT, ac->system_carg);

	setutxent();
	while ((log = getutxent()) != NULL)
		utmp_parse(log);
	endutxent();
}

void get_memory_usage_hw(void)
{
	/* memory_usage_hw omitted on FreeBSD (duplicates memory_usage); keep per-device swap. */
	kvm_t *kd = kvm_open(NULL, NULL, NULL, O_RDONLY, "kvm_open");
	if (kd) {
		/*
		 * kvm_getswapinfo(3): fills up to maxswap-1 per-device slots;
		 * return value is the device count (excludes totals). Grand
		 * totals are written at swap[nswap] and are NOT part of the
		 * return value — do not treat index 0 as totals when maxswap>1.
		 */
		struct kvm_swap swaps[16];
		int maxswap = (int)(sizeof(swaps) / sizeof(swaps[0]));
		int nswap;
		int i;
		int pagesize = getpagesize();

		nswap = kvm_getswapinfo(kd, swaps, maxswap, 0);
		if (nswap >= 0) {
			for (i = 0; i < nswap; i++) {
				uint64_t size_bytes = (uint64_t)swaps[i].ksw_total * (uint64_t)pagesize;
				uint64_t used_bytes = (uint64_t)swaps[i].ksw_used * (uint64_t)pagesize;
				uint64_t free_bytes = size_bytes >= used_bytes ? size_bytes - used_bytes : 0;
				char *dev = swaps[i].ksw_devname[0] ? swaps[i].ksw_devname : (char *)"unknown";

				metric_add_labels2("swap_device_bytes", &size_bytes, DATATYPE_UINT,
					ac->system_carg, "device", dev, "type", "size");
				metric_add_labels2("swap_device_bytes", &used_bytes, DATATYPE_UINT,
					ac->system_carg, "device", dev, "type", "used");
				metric_add_labels2("swap_device_bytes", &free_bytes, DATATYPE_UINT,
					ac->system_carg, "device", dev, "type", "free");
			}
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
	size_t names_sz = 0, cnt_sz = 0, name_len;
	char *names = NULL;
	unsigned long *cnt = NULL;
	int nintr, nnames, i;

	/* FreeBSD hw.intrcnt is a flat u_long[] parallel to hw.intrnames slots
	 * (vmstat -i), not a [irq][cpu] matrix. Per-CPU IPI rows use names like
	 * "cpuN:...". extended_mode has no real per-CPU matrix to export. */
	(void)extended_mode;

	if (sysctlbyname("hw.intrnames", NULL, &names_sz, NULL, 0) != 0)
		return;
	if (sysctlbyname("hw.intrcnt", NULL, &cnt_sz, NULL, 0) != 0)
		return;
	if (!names_sz || !cnt_sz)
		return;

	names = malloc(names_sz);
	cnt = malloc(cnt_sz);
	if (!names || !cnt)
		goto out;

	if (sysctlbyname("hw.intrnames", names, &names_sz, NULL, 0) != 0)
		goto out;
	if (sysctlbyname("hw.intrcnt", cnt, &cnt_sz, NULL, 0) != 0)
		goto out;

	/* Flat u_long[] parallel to fixed-width name slots (width varies by arch). */
	nintr = (int)(cnt_sz / sizeof(unsigned long));
	if (!nintr)
		goto out;
	if (names_sz % (size_t)nintr == 0)
		name_len = names_sz / (size_t)nintr;
	else {
		name_len = INTRNAME_LEN;
		nnames = (int)(names_sz / name_len);
		if (nnames <= 0)
			goto out;
		if (nnames < nintr)
			nintr = nnames;
	}
	if (!name_len)
		goto out;

	for (i = 0; i < nintr; i++) {
		char code[16];
		char description[64];
		uint64_t value;
		size_t copy_len;

		snprintf(code, sizeof(code), "%d", i);
		copy_len = name_len < sizeof(description) - 1 ? name_len : sizeof(description) - 1;
		memcpy(description, names + (size_t)i * name_len, copy_len);
		description[copy_len] = '\0';
		normalize_spaces(description);
		if (description[0] == '\0')
			continue;

		value = (uint64_t)cnt[i];
		metric_add_labels2("interrupts_by_irq_total", &value, DATATYPE_UINT, ac->system_carg,
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
