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

		unsigned char phys_addr[5];
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
		char speed[20];
		snprintf(socket_num, 2, "%d", i);
		snprintf(speed, 19, "%d", cinfo->speed);
		metric_add_labels3("cpu_model", &val, DATATYPE_UINT, ac->system_carg, "model", cinfo[i].model, "socket", socket_num, "speed", speed);
	}
	uv_free_cpu_info(cinfo, count);
}
