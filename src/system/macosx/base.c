#ifdef __APPLE__
#include "main.h"
#include "system/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <mach/mach.h>
#include <mach/vm_statistics.h>
#include <mach/mach_host.h>

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

void get_memory_usage_hw(void)
{
	int64_t physical_memory = 0;
	size_t length = sizeof(physical_memory);
	int mib[2] = {CTL_HW, HW_MEMSIZE};
	vm_size_t page_size;
	mach_port_t mach_port;
	mach_msg_type_number_t count;
	vm_statistics64_data_t vm_stats;
	int64_t free_memory, active_memory, inact_memory, wire_memory;
	int64_t avail_memory, usage_memory;
	struct xsw_usage swap = {0};
	size_t swap_sz = sizeof(swap);

	sysctl(mib, 2, &physical_memory, &length, NULL, 0);

	mach_port = mach_host_self();
	count = HOST_VM_INFO64_COUNT;
	if (host_page_size(mach_port, &page_size) != KERN_SUCCESS
	    || host_statistics64(mach_port, HOST_VM_INFO64, (host_info64_t)&vm_stats, &count) != KERN_SUCCESS)
		return;

	free_memory = (int64_t)vm_stats.free_count * (int64_t)page_size;
	active_memory = (int64_t)vm_stats.active_count * (int64_t)page_size;
	inact_memory = (int64_t)vm_stats.inactive_count * (int64_t)page_size;
	wire_memory = (int64_t)vm_stats.wire_count * (int64_t)page_size;
	avail_memory = free_memory + inact_memory + (int64_t)vm_stats.speculative_count * (int64_t)page_size;
	usage_memory = physical_memory - avail_memory;

	metric_add_labels("memory_usage_hw", &physical_memory, DATATYPE_INT, ac->system_carg, "type", "total");
	metric_add_labels("memory_usage_hw", &avail_memory, DATATYPE_INT, ac->system_carg, "type", "available");
	metric_add_labels("memory_usage_hw", &active_memory, DATATYPE_INT, ac->system_carg, "type", "active");
	metric_add_labels("memory_usage_hw", &inact_memory, DATATYPE_INT, ac->system_carg, "type", "inactive");
	metric_add_labels("memory_usage_hw", &free_memory, DATATYPE_INT, ac->system_carg, "type", "free");
	metric_add_labels("memory_usage_hw", &wire_memory, DATATYPE_INT, ac->system_carg, "type", "wire");
	metric_add_labels("memory_usage_hw", &usage_memory, DATATYPE_INT, ac->system_carg, "type", "usage");

	if (sysctlbyname("vm.swapusage", &swap, &swap_sz, NULL, 0) == 0) {
		int64_t swap_total = (int64_t)swap.xsu_total;
		int64_t swap_used = (int64_t)swap.xsu_used;
		int64_t swap_free = (int64_t)swap.xsu_avail;

		metric_add_labels("memory_usage_hw", &swap_total, DATATYPE_INT, ac->system_carg, "type", "swap_total");
		metric_add_labels("memory_usage_hw", &swap_free, DATATYPE_INT, ac->system_carg, "type", "swap_free");
		metric_add_labels("memory_usage_hw", &swap_used, DATATYPE_INT, ac->system_carg, "type", "swap_usage");
	}
}
#endif
