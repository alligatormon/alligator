#include <sys/param.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <inttypes.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include "main.h"
#include "common/selector.h"
#include "events/context_arg.h"
#include "common/logs.h"
#include "system/common.h"

#ifndef __linux__
#include <kenv.h>
#endif

/* The SMBIOS entry point structure can be located by searching for the anchor-string
   on paragraph (16-byte) boundaries within the physical memory address range 0xf0000
   to 0xfffff */
#define EPS_RANGE_START		0xf0000
#define EPS_RANGE_LEN		0x10000
#define EPS_RANGE_STEP		0x10
#define EPS_ANCHOR		"_SM_"

#define STRINGS_MAXN		256

#pragma pack(1)
struct smbios_eps {
	uint8_t anchor[4];
	uint8_t checksum;
	uint8_t length;
	uint8_t major_version;
	uint8_t minor_version;
	uint16_t max_size;
	uint8_t revision;
	uint8_t formatted_area[5];
	uint8_t ianchor[5];
	uint8_t ichecksum;
	uint16_t table_length;
	uint32_t table_address;
	uint16_t struct_count;
	uint8_t bcd_revision;
};
#pragma pack()

#pragma pack(1)
struct smbios_h {
	uint8_t type;
	uint8_t length;
	uint16_t handle;
};
#pragma pack()

#pragma pack(1)
struct smbios_s {
	struct smbios_h h;
	union {
		struct {
			uint8_t vendor;
			uint8_t version;
			uint8_t reserved[2];
			uint8_t release_date;
		} bios;
		struct {
			uint8_t manufacturer;
			uint8_t product_name;
			uint8_t version;
			uint8_t serial_number;
		} system;
		struct {
			uint8_t manufacturer;
			uint8_t product_name;
			uint8_t version;
			uint8_t serial_number;
			uint8_t asset_tag;
			uint8_t future_flags;
			uint8_t location;
			uint16_t chassis_handle;
			uint8_t type;
		} module;
		struct {
			uint8_t manufacturer;
			uint8_t type;
			uint8_t version;
			uint8_t serial_number;
			uint8_t asset_tag;
		} chassis;
		struct {
			uint8_t socket_designation;
			uint8_t type;
			uint8_t family;
			uint8_t manufacturer;
			uint8_t id[8];
			uint8_t version;
			uint8_t voltage;
			uint16_t external_clock;
			uint16_t max_speed;
			uint16_t current_speed;
			uint8_t status;
			uint8_t upgrade;
			uint16_t l1_cache_handle;
			uint16_t l2_cache_handle;
			uint16_t l3_cache_handle;
			uint8_t serial_number;
			uint8_t asset_tag;
			uint8_t part_number;
			uint8_t core_count;
			uint8_t core_enabled;
			uint8_t thread_count;
			uint16_t characteristics;
			uint16_t family2;
		} processor;
		struct {
			uint16_t physical_handle;
			uint16_t ecc_handle;
			uint16_t total_width;
			uint16_t data_width;
			uint16_t size;
			uint8_t form_factor;
			uint8_t device_set;
			uint8_t device_locator;
			uint8_t bank_locator;
			uint8_t type;
			uint16_t type_detail;
			uint16_t speed;
			uint8_t manufacturer;
			uint8_t serial_number;
			uint8_t asset_tag;
			uint8_t part_number;
		} memory;
		struct {
			uint8_t interface_type;
			uint8_t bcd_revision;
			uint8_t i2c_slave_address;
			uint8_t nv_storage_address;
			uint64_t base_address;
			uint8_t base_address_modifier;
			uint8_t interrupt_number;
		} ipmi;
	} u;
};
#pragma pack()

char *memory_form_by_factor(uint8_t byte) {
	switch (byte) {
		case 3: return "SIMM";
		case 4: return "SIP";
		case 5: return "Chip";
		case 6: return "DIP";
		case 7: return "ZIP";
		case 8: return "Proprietary Card";
		case 9: return "DIMM";
		case 10: return "TSOP";
		case 11: return "Row of chips";
		case 12: return "RIMM";
		case 13: return "SODIMM";
		case 14: return "SRIMM";
		case 15: return "FB-DIMM";
		case 16: return "Die";
		default: return "unknown";
	}
}

char *memory_by_type(uint8_t byte) {
	switch (byte) {
		case 2: return "Standard";
		case 3: return "Fast Page Mode";
		case 4: return "EDO";
		case 5: return "Parity";
		case 6: return "ECC";
		case 7: return "SIMM";
		case 8: return "DIMM";
		case 9: return "Burst EDO";
		case 10: return "SDRAM";
		default: return "unknown";
	}
}

char *get_baseboard_by_type(uint8_t type) {
	switch (type) {
		case 3: return "Server Blade";
		case 4: return "Connectivity Switch";
		case 5: return "System Management Module";
		case 6: return "Processor Module";
		case 7: return "I/O Module";
		case 8: return "Memory Module";
		case 9: return "Daughter board";
		case 10: return "Motherboard";
		case 11: return "Processor/Memory Module";
		case 12: return "Processor/IO Module";
		case 13: return "Interconnect board";
		default: return "unknown";
	}
}

char *get_chassis_by_type(uint8_t type) {
	switch (type) {
		case 3: return "Desktop";
		case 4: return "Low Profile Desktop";
		case 5: return "Pizza Box";
		case 6: return "Mini Tower";
		case 7: return "Tower";
		case 8: return "Portable";
		case 9: return "Laptop";
		case 10: return "Notebook";
		case 11: return "Hand Held";
		case 12: return "Docking Station";
		case 13: return "All in One";
		case 14: return "Sub Notebook";
		case 15: return "Space-saving";
		case 16: return "Lunch Box";
		case 17: return "Main Server Chassis";
		case 18: return "Expansion Chassis";
		case 19: return "SubChassis";
		case 20: return "Bus Expansion Chassis";
		case 21: return "Peripheral Chassis";
		case 22: return "RAID Chassis";
		case 23: return "Rack Mount Chassis";
		case 24: return "Sealed-case PC";
		case 25: return "Multi-system chassis";
		case 26: return "Compact PCI";
		case 27: return "Advanced TCA";
		case 28: return "Blade";
		case 29: return "Blade Enclosure";
		case 30: return "Tablet";
		case 31: return "Convertible";
		case 32: return "Detachable";
		case 33: return "IoT Gateway";
		case 34: return "Embedded PC";
		case 35: return "Mini PC";
		case 36: return "Stick PC";
		default: return "unknown";
	}
}

char *get_processor_status_by_id(uint8_t id) {
	if (id >= 128) {
		id -= 128;
	}
	if (id >= 64) {
		id -= 64;
	}
	switch (id) {
		case 1: return "Enabled";
		case 2: return "Disabled by User through BIOS Setup";
		case 3: return "CPU Disabled By BIOS (POST Error)";
		case 4: return "CPU is Idle, waiting to be enabled";
		default: return "unknown";
	}
}

int smbios_read_memory(int memfd, off_t offset, size_t len, uint8_t **buf) {
	ssize_t nbytes;

	if (lseek(memfd, offset, SEEK_SET) < 0) {
		carglog(ac->system_carg, L_ERROR, "%s: lseek\n", __FUNCTION__);
		return(0);
	}

	nbytes = read(memfd, *buf, len);
	if (nbytes < 0) {
		carglog(ac->system_carg, L_ERROR,"%s: read\n", __FUNCTION__);
		return(0);
	}
	if ((size_t)nbytes != len) {
		carglog(ac->system_carg, L_ERROR, "%s: read: %lu bytes requested but only %lu bytes actually read\n", __FUNCTION__, (u_long)len, (u_long)nbytes);
		return(0);
	}

	return(1);
}

int smbios_eval_checksum(const uint8_t *buf, size_t len) {
	const uint8_t *p, *pe;
	uint8_t sum;

	p = buf;
	pe = p + len;
	sum = 0;
	while (p < pe)
		sum += *p++;

	return(sum == 0);
}

int smbios_lookfor_table(int memfd, struct smbios_eps *eps_buf) {
	uint8_t *buf;
	size_t offset;
	int f_found;
	struct smbios_eps *eps;
	uint64_t uefi_smbios_addr = 0;
#ifndef __linux__
	char kname[] = "hint.smbios.0.mem", kval[20], *eptr;
	int klen;
#else
	FILE * fp;
#endif

	if (!(buf = malloc(EPS_RANGE_LEN))) {
		carglog(ac->system_carg, L_ERROR, "%s: malloc(%lu)\n", __FUNCTION__, (u_long)EPS_RANGE_LEN);
		return(0);
	}

	if (!smbios_read_memory(memfd, EPS_RANGE_START, EPS_RANGE_LEN, &buf)) {
		free(buf);
		return(0);
	}

	f_found = 0;
	eps = NULL;
	for (offset = 0; offset < EPS_RANGE_LEN - sizeof(*eps); offset += EPS_RANGE_STEP) {
		eps = (struct smbios_eps *)(buf + offset);
		if (!memcmp(eps->anchor, EPS_ANCHOR, sizeof(eps->anchor))) {
			carglog(ac->system_carg, L_INFO, "%s: SMBIOS EPS found at 0x%08lx\n", __FUNCTION__, (u_long)offset + EPS_RANGE_START);
			f_found = 1;
			break;
		}
	}
	if (!f_found) {
#ifndef __linux__
		bzero(kval, sizeof(kval));
		eptr = NULL;
		if ((klen = kenv(KENV_GET, kname, kval, sizeof(kval))) > 0 &&
			klen < (int) sizeof(kval) &&
			!kval[klen] &&
			(uefi_smbios_addr = strtoull(kval, &eptr, 0)) &&
			!*eptr) {
#else
		if ((fp = fopen("/sys/firmware/efi/systab", "r")) != NULL && fscanf(fp, "%"PRIu64, &uefi_smbios_addr) == 1 && uefi_smbios_addr) {
#endif
			carglog(ac->system_carg, L_INFO, "SMBIOS EPS not found at default location" ", but we're got alternative addr 0x%016lx " "from UEFI\n", uefi_smbios_addr);
			if (smbios_read_memory(memfd, uefi_smbios_addr, sizeof(*eps), &buf)) {
				eps = (struct smbios_eps *) buf;
				if (!memcmp(eps->anchor, EPS_ANCHOR, sizeof(eps->anchor))) {
					carglog(ac->system_carg, L_INFO, "%s: SMBIOS EPS found\n", __FUNCTION__);
					f_found = 1;
				}
			}
#ifdef __linux__
			fclose(fp);
#endif
		}
	}
	if (!f_found) {
		free(buf);
		return(0);
	}

	if (eps->length < sizeof(*eps)) {
		carglog(ac->system_carg, L_ERROR, "%s: SMBIOS EPS has too small length (%lu bytes)\n", __FUNCTION__, (u_long)eps->length);
		free(buf);
		return(0);
	}

	if (!smbios_eval_checksum((uint8_t *)eps, eps->length)) {
		carglog(ac->system_carg, L_ERROR, "%s: SMBIOS EPS has wrong checksum\n", __FUNCTION__);
		free(buf);
		return(0);
	}

	if (!eps->table_length) {
		carglog(ac->system_carg, L_ERROR, "%s: SMBIOS table has zero length\n", __FUNCTION__);
		free(buf);
		return(0);
	}

	carglog(ac->system_carg, L_INFO, "%s: SMBIOS version %u.%u present\n", __FUNCTION__, (u_int)eps->major_version, (u_int)eps->minor_version);
	carglog(ac->system_carg, L_INFO, "%s: SMBIOS table located at 0x%08lx\n", __FUNCTION__, (u_long)eps->table_address);
	carglog(ac->system_carg, L_INFO, "%s: %lu structures occupying %lu bytes\n", __FUNCTION__, (u_long)eps->struct_count, (u_long)eps->table_length);

	memcpy(eps_buf, eps, sizeof(*eps));

	free(buf);
	return(1);
}

int is_field_exists(struct smbios_s *smbios, void *field) {
	return (field - (void*)smbios) + sizeof(field) <= smbios->h.length;
}

int smbios_decode_strings(struct smbios_s *smbios, char ***strings, u_int *strings_n) {
	uint8_t *p;
	u_int n;

	n = 0;
	p = (uint8_t *)smbios;
	p += smbios->h.length;
	while (*p) {
		if (n == STRINGS_MAXN) {
			carglog(ac->system_carg, L_ERROR, "%s: Too many strings in unformed section " "(maximum %u allowed)\n", __FUNCTION__, (u_int)STRINGS_MAXN);
			return(0);
		}
		(*strings)[n++] = (char *)p;

		do {
			if (*p < 0x20)
				*p = ' ';
		} while (*(++p));
		p++;
	}

	*strings_n = n;
	return(1);
}

void smbios_decode_struct(struct smbios_s *smbios) {
	char *strings[STRINGS_MAXN], **str;
	u_int strings_n;
	uint64_t size = 0;

	uint16_t processors = 0;
	char processor_str[11];
	uint64_t okval = 1;
	uint64_t val = 1;

	str = strings;
	if (!smbios_decode_strings(smbios, &str, &strings_n))
		return;

	switch (smbios->h.type) {
	case 0:
		if (smbios->u.bios.vendor)
			metric_add_labels("bios_vendor", &okval, DATATYPE_UINT, ac->system_carg, "vendor", trim_whitespaces(str[smbios->u.bios.vendor-1]));
		if (smbios->u.bios.version)
			metric_add_labels("bios_version", &okval, DATATYPE_UINT, ac->system_carg, "version", trim_whitespaces(str[smbios->u.bios.version-1]));
		break;
	case 1:
		if (smbios->u.system.manufacturer)
			metric_add_labels("system_manufacturer", &okval, DATATYPE_UINT, ac->system_carg, "manufacturer", trim_whitespaces(str[smbios->u.system.manufacturer-1]));
		if (smbios->u.system.product_name) {
			char *sys_product_name = trim_whitespaces(str[smbios->u.system.product_name-1]);
			metric_add_labels("system_product_name", &okval, DATATYPE_UINT, ac->system_carg, "name", sys_product_name);
			if (!strncmp(sys_product_name, "OpenStack", 9))
				ac->system_platform = PLATFORM_OPENSTACK;
			else if (!strncmp(sys_product_name, "KVM", 3))
				ac->system_platform = PLATFORM_KVM;
		}
		if (smbios->u.system.version)
			metric_add_labels("system_version", &okval, DATATYPE_UINT, ac->system_carg, "version", trim_whitespaces(str[smbios->u.system.version-1]));
		if (smbios->u.system.serial_number)
			metric_add_labels("system_serial", &okval, DATATYPE_UINT, ac->system_carg, "serial", trim_whitespaces(str[smbios->u.system.serial_number-1]));
		break;
	case 2:
		if (smbios->u.module.type == 10) {
			if (smbios->u.module.manufacturer)
				metric_add_labels("baseboard_manufacturer", &okval, DATATYPE_UINT, ac->system_carg, "manufacturer", trim_whitespaces(str[smbios->u.module.manufacturer-1]));
			if (smbios->u.module.product_name)
				metric_add_labels("baseboard_product_name", &okval, DATATYPE_UINT, ac->system_carg, "name", trim_whitespaces(str[smbios->u.module.product_name-1]));
			if (smbios->u.module.version)
				metric_add_labels("baseboard_version", &okval, DATATYPE_UINT, ac->system_carg, "version", trim_whitespaces(str[smbios->u.module.version-1]));
			if (smbios->u.module.serial_number)
				metric_add_labels("baseboard_serial", &okval, DATATYPE_UINT, ac->system_carg, "serial", trim_whitespaces(str[smbios->u.module.serial_number-1]));
			if (smbios->u.module.asset_tag)
				metric_add_labels("baseboard_asset_tag", &okval, DATATYPE_UINT, ac->system_carg, "tag", trim_whitespaces(str[smbios->u.module.asset_tag-1]));
			if (smbios->u.module.location)
				metric_add_labels("baseboard_location", &okval, DATATYPE_UINT, ac->system_carg, "location", trim_whitespaces(str[smbios->u.module.location-1]));
		}
		else
		{
			metric_add_labels7("module_description", &okval, DATATYPE_UINT, ac->system_carg,
				"manufacturer", smbios->u.module.manufacturer ? trim_whitespaces(str[smbios->u.module.manufacturer-1]) : "",
				"type", get_baseboard_by_type(smbios->u.module.type),
				"name", smbios->u.module.product_name ? trim_whitespaces(str[smbios->u.module.product_name-1]) : "",
				"version", smbios->u.module.version ? trim_whitespaces(str[smbios->u.module.version-1]) : "",
				"serial", smbios->u.module.serial_number ? trim_whitespaces(str[smbios->u.module.serial_number-1]) : "",
				"tag", smbios->u.module.asset_tag ? trim_whitespaces(str[smbios->u.module.asset_tag-1]) : "",
				"location", smbios->u.module.location ? trim_whitespaces(str[smbios->u.module.location-1]) : ""
			);
		}
		break;
	case 3:
		if (smbios->u.chassis.manufacturer)
			metric_add_labels("chassis_manufacturer", &okval, DATATYPE_UINT, ac->system_carg, "manufacturer", trim_whitespaces(str[smbios->u.chassis.manufacturer-1]));
		if (smbios->u.chassis.version)
			metric_add_labels("chassis_version", &okval, DATATYPE_UINT, ac->system_carg, "version", trim_whitespaces(str[smbios->u.chassis.version-1]));
		if (smbios->u.chassis.serial_number)
			metric_add_labels("chassis_serial", &okval, DATATYPE_UINT, ac->system_carg, "serial", trim_whitespaces(str[smbios->u.chassis.serial_number-1]));
		if (smbios->u.chassis.asset_tag)
			metric_add_labels("chassis_asset_tag", &okval, DATATYPE_UINT, ac->system_carg, "tag", trim_whitespaces(str[smbios->u.chassis.asset_tag-1]));
		metric_add_labels("chassis_type", &okval, DATATYPE_UINT, ac->system_carg, "type", get_chassis_by_type(smbios->u.chassis.type));
		break;
	case 4:
		snprintf(processor_str, 10, "CPU %"PRIu16, processors);
		if (smbios->u.processor.manufacturer)
			metric_add_labels2("processor_manufacturer", &okval, DATATYPE_UINT, ac->system_carg, "socket", processor_str, "manufacturer", trim_whitespaces(str[smbios->u.processor.manufacturer-1]));
		if (smbios->u.processor.version)
			metric_add_labels2("processor_version", &okval, DATATYPE_UINT, ac->system_carg, "socket", processor_str, "version", trim_whitespaces(str[smbios->u.processor.version-1]));
		if (smbios->u.processor.serial_number)
			metric_add_labels2("processor_serial", &okval, DATATYPE_UINT, ac->system_carg, "socket", processor_str, "serial", trim_whitespaces(str[smbios->u.processor.serial_number-1]));
		if (smbios->u.processor.asset_tag)
			metric_add_labels2("processor_asset_tag", &okval, DATATYPE_UINT, ac->system_carg, "socket", processor_str, "tag", trim_whitespaces(str[smbios->u.processor.asset_tag-1]));
		if (smbios->u.processor.part_number)
			metric_add_labels2("processor_part_number", &okval, DATATYPE_UINT, ac->system_carg, "socket", processor_str, "part_number", trim_whitespaces(str[smbios->u.processor.part_number-1]));
		metric_add_labels2("processor_status", &okval, DATATYPE_UINT, ac->system_carg, "socket", processor_str, "status", get_processor_status_by_id(smbios->u.processor.status));

		val = smbios->u.processor.external_clock;
		metric_add_labels("processor_external_clock", &val, DATATYPE_UINT, ac->system_carg, "socket", processor_str);

		val = smbios->u.processor.max_speed;
		metric_add_labels("processor_max_speed", &val, DATATYPE_UINT, ac->system_carg, "socket", processor_str);

		val = smbios->u.processor.current_speed;
		metric_add_labels("processor_current_speed", &val, DATATYPE_UINT, ac->system_carg, "socket", processor_str);

		val = smbios->u.processor.core_count;
		metric_add_labels("processor_core_count", &val, DATATYPE_UINT, ac->system_carg, "socket", processor_str);

		val = smbios->u.processor.core_enabled;
		metric_add_labels("processor_core_enabled", &val, DATATYPE_UINT, ac->system_carg, "socket", processor_str);

		val = smbios->u.processor.thread_count;
		metric_add_labels("processor_thread_count", &val, DATATYPE_UINT, ac->system_carg, "socket", processor_str);

		++processors;
		break;
	case 17:
		if (is_field_exists(smbios, &smbios->u.memory.size)) {
			size = smbios->u.memory.size;
			if (size & 0x8000)
				size &= ~(0x8000);
			else
				size *= 1024;
			//printf("memory_size_kb %llu\n", size);
		}
		val = size;
		metric_add_labels8("memory_module_size_kb", &val, DATATYPE_UINT, ac->system_carg,
			"device_locator", smbios->u.memory.device_locator ? trim_whitespaces(str[smbios->u.memory.device_locator-1]) : "",
			"bank_locator", smbios->u.memory.bank_locator ? trim_whitespaces(str[smbios->u.memory.bank_locator-1]) : "",
			"manufacturer", smbios->u.memory.manufacturer ? trim_whitespaces(str[smbios->u.memory.manufacturer-1]) : "",
			"serial", smbios->u.memory.serial_number ? trim_whitespaces(str[smbios->u.memory.serial_number-1]) : "",
			"tag", smbios->u.memory.asset_tag ? trim_whitespaces(str[smbios->u.memory.asset_tag-1]) : "",
			"part_number", smbios->u.memory.part_number ? trim_whitespaces(str[smbios->u.memory.part_number-1]) : "",
			"form_factor", memory_form_by_factor(smbios->u.memory.form_factor),
			"type", memory_by_type(smbios->u.memory.type)
		);

		val = smbios->u.memory.speed;
		metric_add_labels8("memory_module_speed", &val, DATATYPE_UINT, ac->system_carg,
			"device_locator", smbios->u.memory.device_locator ? trim_whitespaces(str[smbios->u.memory.device_locator-1]) : "",
			"bank_locator", smbios->u.memory.bank_locator ? trim_whitespaces(str[smbios->u.memory.bank_locator-1]) : "",
			"manufacturer", smbios->u.memory.manufacturer ? trim_whitespaces(str[smbios->u.memory.manufacturer-1]) : "",
			"serial", smbios->u.memory.serial_number ? trim_whitespaces(str[smbios->u.memory.serial_number-1]) : "",
			"tag", smbios->u.memory.asset_tag ? trim_whitespaces(str[smbios->u.memory.asset_tag-1]) : "",
			"part_number", smbios->u.memory.part_number ? trim_whitespaces(str[smbios->u.memory.part_number-1]) : "",
			"form_factor", memory_form_by_factor(smbios->u.memory.form_factor),
			"type", memory_by_type(smbios->u.memory.type)
		);
		break;
	}
}

void smbios_read_table(int memfd, const struct smbios_eps *eps) {
	uint8_t *buf, *p, *pe;
	u_int i;
	struct smbios_s *smbios;

	if (!(buf = malloc(eps->table_length))) {
		carglog(ac->system_carg, L_ERROR, "%s: malloc(%lu)\n", __FUNCTION__, (u_long)eps->table_length);
		return;
	}

	if (!smbios_read_memory(memfd, eps->table_address, eps->table_length, &buf)) {
		free(buf);
		return;
	}

	p = buf;
	pe = p + eps->table_length;
	for (i = 1; i <= eps->struct_count; i++) {
		carglog(ac->system_carg, L_INFO, "%s: Processing structure %u of %u\n", __FUNCTION__, i, (u_int)eps->struct_count);
		smbios = (struct smbios_s *)p;

		if ((size_t)(pe - p) < sizeof(smbios->h) + 2 || (size_t)(pe - p) < (size_t)smbios->h.length + 2)
		{
			carglog(ac->system_carg, L_INFO, "%s: Formatted section extends beyond the table\n", __FUNCTION__);
			free(buf);
			return;
		}
		if (smbios->h.length < sizeof(smbios->h)) {
			carglog(ac->system_carg, L_INFO, "%s: Formatted section too short (%lu bytes)\n", __FUNCTION__, (u_long)smbios->h.length);
			free(buf);
			return;
		}

		p += smbios->h.length + 2;
		while (*(p - 1) || *(p - 2)) {
			if (p == pe) {
				carglog(ac->system_carg, L_ERROR, "%s: Unformed section extends beyond the table\n", __FUNCTION__);
				free(buf);
				return;
			}
			p++;
		}

		carglog(ac->system_carg, L_INFO, "%s: Type %u, Handle 0x%04x, %lu/%lu bytes\n", __FUNCTION__, (u_int)smbios->h.type, (u_int)smbios->h.handle, (u_long)smbios->h.length, (u_long)(p - (uint8_t *)smbios - smbios->h.length));

		smbios_decode_struct(smbios);
	}

	free(buf);
}

void get_smbios() {
	int memfd;
	struct smbios_eps eps;

	if ((memfd = open("/dev/mem", O_RDONLY)) < 0)
		return;

	if (!smbios_lookfor_table(memfd, &eps))
	{
		close(memfd);
		return;
	}

	smbios_read_table(memfd, &eps);

	close(memfd);
}
