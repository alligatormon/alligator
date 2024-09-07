#include "main.h"
void ipaddr_info() {
	char addr[512];
	char mask[512];
	char phys[512];
	uv_interface_address_t *info;
	int count, i;
	uint64_t val = 1;

	uv_interface_addresses(&info, &count);
	i = count;

	int64_t count64 = count;
	metric_add_auto("iface_num", &count64, DATATYPE_INT, ac->system_carg);
	while (i--) {
		uv_interface_address_t interface = info[i];

		unsigned char phys_addr[6];
		phys_addr[0] = interface.phys_addr[0];
		phys_addr[1] = interface.phys_addr[1];
		phys_addr[2] = interface.phys_addr[2];
		phys_addr[3] = interface.phys_addr[3];
		phys_addr[4] = interface.phys_addr[4];
		phys_addr[5] = interface.phys_addr[5];

		sprintf(phys, "%02X:%02X:%02X:%02X:%02X:%02X", phys_addr[0], phys_addr[1], phys_addr[2], phys_addr[3], phys_addr[4], phys_addr[5]);
		//printf("1 %02X:%02X:%02X:%02X:%02X:%02X\n", phys_addr[0], phys_addr[1], phys_addr[2], phys_addr[3], phys_addr[4], phys_addr[5]);
		//printf("2 %u:%u:%u:%u:%u:%u\n", phys_addr[0], phys_addr[1], phys_addr[2], phys_addr[3], phys_addr[4], phys_addr[5]);

		if (interface.address.address4.sin_family == AF_INET) {
			uv_ip4_name(&interface.address.address4, addr, sizeof(addr));
			uv_ip4_name(&interface.netmask.netmask4, mask, sizeof(mask));
			metric_add_labels5("interface_address", &val, DATATYPE_UINT, ac->system_carg, "version", "IPv4", "address", addr, "phys_addr", phys, "iface", interface.name, "mask", mask);
		}
		else if (interface.address.address4.sin_family == AF_INET6) {
			uv_ip6_name(&interface.address.address6, addr, sizeof(addr));
			uv_ip6_name(&interface.netmask.netmask6, mask, sizeof(mask));
			metric_add_labels5("interface_address", &val, DATATYPE_UINT, ac->system_carg, "version", "IPv6", "address", addr, "phys_addr", phys, "iface", interface.name, "mask", mask);
		}
	}

	uv_free_interface_addresses(info, count);

}

void hw_cpu_info()
{
	int count;
	uv_cpu_info_t *cinfo;
	uv_cpu_info(&cinfo, &count);
	uint64_t val = 1;
	int i = count;
	while (i--)
	{
		char socket_num[3];
		snprintf(socket_num, 2, "%d", i);
		metric_add_labels2("cpu_model", &val, DATATYPE_UINT, ac->system_carg, "model", cinfo[i].model, "socket", socket_num);
	}
	uv_free_cpu_info(cinfo, count);
}

void get_utsname()
{
	uv_utsname_t buf;
	uv_os_uname(&buf);
	uint64_t val = 1;
	//printf("sysname: %s, relese: %s, version: %s, machine: %s\n", buf.sysname, buf.release, buf.version, buf.machine);
	metric_add_labels("machine_arch", &val, DATATYPE_UINT, ac->system_carg, "arch", buf.machine);
}

void system_initialize()
{
	extern aconf *ac;
	ac->system_base = 0;
	ac->system_interrupts = 0;
	ac->system_network = 0;
	ac->system_disk = 0;
	ac->system_process = 0;
	ac->system_cadvisor = 0;
	ac->system_smart = 0;
	ac->system_carg = calloc(1, sizeof(*ac->system_carg));
	ac->system_carg->ttl = 300;
	ac->system_carg->curr_ttl = 0;
	ac->scs = calloc(1, sizeof(system_cpu_stats));
	ac->system_sysfs = strdup("/sys/");
	ac->system_procfs = strdup("/proc/");
	ac->system_rundir = strdup("/run/");
	ac->system_usrdir = strdup("/usr/");
	ac->system_etcdir = strdup("/etc/");
	ac->system_pidfile = calloc(1, sizeof(*ac->system_pidfile));

	ac->process_match = calloc(1, sizeof(match_rules));
	ac->process_match->hash = alligator_ht_init(NULL);

	ac->packages_match = calloc(1, sizeof(match_rules));
	ac->packages_match->hash = alligator_ht_init(NULL);

	ac->services_match = calloc(1, sizeof(match_rules));
	ac->services_match->hash = alligator_ht_init(NULL);

	ac->system_userprocess = alligator_ht_init(NULL);
	ac->system_groupprocess = alligator_ht_init(NULL);

	ac->system_sysctl = alligator_ht_init(NULL);
}
