#ifdef __linux__
#include <linux/nvme_ioctl.h>
#include <sys/ioctl.h>
#include "common/selector.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

uint64_t nvme_ioctl_stat(char *dev, char *controller, char *disk) {
	int fd = open(dev, O_RDWR);
	if (fd < 0) {
	    carglog(ac->system_carg, L_ERROR, "%s path open error: %s: %s\n", __FUNCTION__, dev, strerror(errno));
		return 0;
	}

	char buf[4096] = {0};
	struct nvme_admin_cmd mib = {0};
	mib.opcode = 0x06; // identify
	mib.nsid = 0;
	mib.addr = (__u64) buf;
	mib.data_len = sizeof(buf);
	mib.cdw10 = 1; // controller

	int ret = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &mib);
	close(fd);
	if (ret) {
	    carglog(ac->system_carg, L_ERROR, "%s path ioctl error: %s: %s\n", __FUNCTION__, dev, strerror(errno));
		return 0;
	}

	uint64_t val = 1;
	char serial[21];
	char model[41];
	char firmware[9];
	strlcpy(model, buf+24, 41);
	strlcpy(serial, buf+4, 21);
	strlcpy(firmware, buf+64, 9);

    //uint64_t size = to_uint64(buf);
    //uint64_t capacity = to_uint64(buf + 8);
    //uint64_t utilization = to_uint64(buf + 16);
	uint64_t composite_temp_warn_thresh = to_uint16(buf + 266);
	uint64_t composite_temp_crit_thresh = to_uint16(buf + 268);
	uint64_t ns_count = to_uint32(buf + 516);
	uint64_t capacity_total = to_uint128(buf + 280);
	uint64_t capacity_unalloc = to_uint128(buf + 296);

	metric_add_labels3("disk_model", &val, DATATYPE_UINT, ac->system_carg, "model", trim_whitespaces(model), "disk", disk, "controller", controller);
	metric_add_labels3("disk_serial", &val, DATATYPE_UINT, ac->system_carg, "serial", trim_whitespaces(serial), "disk", disk, "controller", controller);
	metric_add_labels3("disk_firmware", &val, DATATYPE_UINT, ac->system_carg, "firmware", trim_whitespaces(firmware), "disk", disk, "controller", controller);
	metric_add_labels4("nvme_temperature_threshold_celsius", &composite_temp_warn_thresh, DATATYPE_UINT, ac->system_carg, "model", trim_whitespaces(model), "disk", disk, "kind", "warning", "controller", controller);
	metric_add_labels4("nvme_temperature_threshold_celsius", &composite_temp_crit_thresh, DATATYPE_UINT, ac->system_carg, "model", trim_whitespaces(model), "disk", disk, "kind", "critical", "controller", controller);
	metric_add_labels3("nvme_total_namespace_count", &ns_count, DATATYPE_UINT, ac->system_carg, "model", trim_whitespaces(model), "disk", disk, "controller", controller);
	metric_add_labels2("disk_size", &capacity_total, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller);
	metric_add_labels2("disk_unallocated", &capacity_unalloc, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller);
	//metric_add_labels2("nvme_ns_capacity_bytes", &capacity, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller);
	//metric_add_labels2("nvme_ns_utilization_bytes", &utilization, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller);
	//metric_add_labels2("nvme_ns_size_bytes", &size, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller);
	return 1;
}

#define SMART_LEN 512

int nvme_smart_get(char *dev, char *controller, char *disk) {
	int fd;
	fd = open(dev, O_RDONLY);
	if (!fd) {
	    carglog(ac->system_carg, L_ERROR, "%s path open error: %s: %s\n", __FUNCTION__, dev, strerror(errno));
		return 0;
	}
	int nsid_id = ioctl(fd, NVME_IOCTL_ID);
	char nsid[6];
	snprintf(nsid, 5, "%d", nsid_id);

	char data[SMART_LEN];
	for (register int i=0; i<SMART_LEN; data[i++] = 0);
	struct nvme_admin_cmd cmd = {
		.opcode = 0x02,
		//.nsid = 0xffffffff,
		.nsid = nsid_id,
		.addr = (__u64)data,
		.data_len = SMART_LEN,
		.cdw10 = 0x007F0002,
	};

	int ret;
	ret= ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
	close(fd);
	if (ret) {
	    carglog(ac->system_carg, L_ERROR, "%s path ioctl error: %s: %s\n", __FUNCTION__, dev, strerror(errno));
		return 0;
	}

	//if (data[0] & 0x1) // spare space has fallen below the threshold
	//if (data[0]&0x2) temperaturehas exceed a critical threshold
	//if (data[0]&0x4) // device reliability has been degraded
	//if (data[0]&0x8) volatile memory backup device has failed

	uint64_t temperature = to_uint16(data+1) - 273;
	metric_add_labels4("nvme_temperature_celsius", &temperature, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid, "sensor", "composite");

	uint64_t spare_avail_pct = data[3];
	metric_add_labels3("nvme_spare_available_percent", &spare_avail_pct, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid);

	uint64_t spare_thresh_pct = data[4];
	metric_add_labels3("nvme_spare_threshold_percent", &spare_thresh_pct, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid);

	uint64_t wear_pct = data[5];
	metric_add_labels3("nvme_lifetime_wear_percent", &wear_pct, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid);

	uint64_t status_crit_endurance_group = data[6];
	metric_add_labels3("nvme_failing_endurance_group", &status_crit_endurance_group, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid);

	uint64_t data_read = to_uint64(data+32) * 1000 * 512;
	metric_add_labels4("nvme_data_transferred", &data_read, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid, "dir", "read");

	uint64_t data_written = to_uint64(data+48) * 1000 * 512;
	metric_add_labels4("nvme_data_transferred", &data_written, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid, "dir", "write");

	uint64_t cmds_read = to_uint64(data+64);
	metric_add_labels4("nvme_commands", &cmds_read, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid, "dir", "read");

	uint64_t cmds_write = to_uint64(data+80);
	metric_add_labels4("nvme_commands", &cmds_write, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid, "dir", "write");

	uint64_t controller_busy_time = to_uint64(data+96);
	metric_add_labels3("nvme_controller_busy_time_minutes", &controller_busy_time, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid);

	uint64_t power_cycles = to_uint64(data+112);
	metric_add_labels3("nvme_power_cycles", &power_cycles, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid);

	uint64_t power_on_hours = to_uint64(data+128);
	metric_add_labels3("nvme_power_on_hours", &power_on_hours, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid);

	uint64_t power_cycles_unsafe = to_uint64(data+144);
	metric_add_labels3("nvme_power_cycles_unsafe", &power_cycles_unsafe, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid);

	uint64_t data_errors = to_uint64(data+160);
	metric_add_labels4("nvme_errors", &data_errors, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid, "kind", "data");

	uint64_t total_errors = to_uint64(data+176);
	metric_add_labels4("nvme_errors", &total_errors, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid, "kind", "total");

	uint64_t temp_warn_time = to_uint32(data+192);
	metric_add_labels4("nvme_temperature_over_threshold_minutes", &temp_warn_time, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid, "kind", "warning");

	uint64_t temp_crit_time = to_uint32(data+196);
	metric_add_labels4("nvme_temperature_over_threshold_minutes", &temp_crit_time, DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid, "kind", "critical");

	uint64_t temps[8];
	memset(&temps, 0, sizeof(temps));
	for (uint8_t i = 0, j = 200; i < 8 && j < 216; ++i) {
		char sensor[2];
		snprintf(sensor, 2, "%hhu", i);
		temps[i] = to_uint16(data+j);
		if (!temps[i])
			break;

		temps[i] -= 273;
		metric_add_labels4("nvme_temperature_celsius", &temps[i], DATATYPE_UINT, ac->system_carg, "disk", disk, "controller", controller, "nsid", nsid, "sensor", sensor);

		j += 2;
	}

	return 0;
}

uint64_t nvme_info()
{
	carglog(ac->system_carg, L_INFO, "system scrape metrics: disks: nvme\n");

	int disk_num = 0;

	char classpath[255];
	snprintf(classpath, 255, "%s/class/nvme", ac->system_sysfs);
	DIR *dir_controller = opendir(classpath);
	struct dirent *controller;
	if (!dir_controller)
	{
		return 0;
	}

	while((controller = readdir(dir_controller)))
	{
		if ( controller->d_name[0] == '.' )
			continue;

		char controllersyspath[2048];
		snprintf(controllersyspath, 2047, "%s/%s", classpath, controller->d_name);

		char controllerpath[2048];
		snprintf(controllerpath, 2047, "%s/%s", "/dev", controller->d_name);

		struct dirent *disk;
		DIR *dir_disk = opendir(controllersyspath);
		if (!dir_disk)
			continue;

	    carglog(ac->system_carg, L_INFO, "system scrape metrics: disks: controller: %s from dev %s\n", controller->d_name, controllerpath);
		disk_num += nvme_ioctl_stat(controllerpath, controller->d_name, controller->d_name);
		while((disk = readdir(dir_disk)))
		{
			if ( strncmp(disk->d_name, controller->d_name, strlen(controller->d_name)) )
				continue;

			char diskpath[2048];
			snprintf(diskpath, 2047, "%s/%s", "/dev", disk->d_name);
	    	carglog(ac->system_carg, L_INFO, "system scrape metrics: disks: controller: %s from dev %s, disk: %s\n", controller->d_name, controllerpath, disk->d_name);
			nvme_smart_get(diskpath, controller->d_name, disk->d_name);
		}
		closedir(dir_disk);
	}
	closedir(dir_controller);

	return disk_num;
}

uint64_t block_info()
{
	struct dirent *entry;
	DIR *dp;
	uint64_t val = 1;

	dp = opendir("/sys/class/block/");
	if (!dp)
		return 0;

	uint64_t disks_num = 0;
	while((entry = readdir(dp)))
	{
		if (entry->d_name[0] == '.')
			continue;
		if (strpbrk(entry->d_name, "0123456789"))
			continue;

		char blockpath[512];
		snprintf(blockpath, 511, "/sys/class/block/%s/device/model", entry->d_name);
		string *disk_model = get_file_content(blockpath, 0);
		if (disk_model)
		{
			++disks_num;
			disk_model->s[strcspn(disk_model->s, "\n\r")] = 0;
			metric_add_labels2("disk_model", &val, DATATYPE_UINT, ac->system_carg, "model", disk_model->s, "disk", entry->d_name);
			string_free(disk_model);
		}
	}
	closedir(dp);

	return disks_num;
}

void disks_info() {
	uint64_t disks_num = 0;
	disks_num += block_info();
	disks_num += nvme_info();
	metric_add_auto("disk_num", &disks_num, DATATYPE_UINT, ac->system_carg);
}
#endif
