#ifdef __APPLE__
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
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <utmpx.h>

extern aconf *ac;

static void emit_platform_label(const char *platform, int8_t mode)
{
	int64_t vl = 1;

	if (!mode)
		return;
	metric_add_labels("server_platform", &vl, DATATYPE_INT, ac->system_carg, "platform", (char *)platform);
}

int8_t get_platform(int8_t mode)
{
	char model[64];
	size_t sz = sizeof(model);

	if (ac->system_platform == PLATFORM_OPENSTACK) {
		emit_platform_label("openstack", mode);
		return PLATFORM_OPENSTACK;
	}
	if (ac->system_platform == PLATFORM_KVM) {
		emit_platform_label("kvm", mode);
		return PLATFORM_KVM;
	}

	if (sysctlbyname("hw.model", model, &sz, NULL, 0) == 0) {
		if (strstr(model, "VirtualMac") || strstr(model, "VMware")) {
			emit_platform_label("kvm", mode);
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
		platform_name = "container";
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
		"version", un.release, "platform", (char *)platform_name);

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
	struct utsname un;
	char version[255] = "";
	char pretty_name[255] = "";
	uint64_t okval = 1;

	if (uname(&un) != 0)
		return;

	strlcpy(version, un.release, sizeof(version));
	snprintf(pretty_name, sizeof(pretty_name), "macOS %s", version);

	metric_add_labels4("os_distribution", &okval, DATATYPE_UINT, ac->system_carg,
		"os", "darwin", "name", "macOS", "version", version, "pretty", pretty_name);
}

static void utmp_parse(struct utmpx *log)
{
	if (!log || log->ut_type != USER_PROCESS)
		return;

	{
		int64_t time = log->ut_tv.tv_sec;
		/* Darwin: ttysNNN matches "tty" → terminal; also treat console as terminal. */
		if (!strncmp(log->ut_line, "tty", 3) || !strcmp(log->ut_line, "console")) {
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

void get_proc_interrupts(int extended_mode)
{
	static int logged;

	(void)extended_mode;
	/* Darwin has no hw.intrnames/hw.intrcnt or /proc/interrupts equivalent. */
	if (!logged) {
		carglog(ac->system_carg, L_INFO,
			"get_proc_interrupts: IRQ breakdown unsupported on Darwin\n");
		logged = 1;
	}
}
#endif
