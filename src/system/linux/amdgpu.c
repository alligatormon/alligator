#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include "main.h"
#include "common/logs.h"
#include "common/selector.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "system/linux/amdgpu.h"

extern aconf *ac;

static void amdgpu_metric_set(context_arg *carg, const char *metric_name, const char *help)
{
	namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, help);
}

void amdgpu_emit_globals(context_arg *carg, uint64_t gpu_count)
{
	if (!carg)
		return;

	amdgpu_metric_set(carg, "amdgpu_gpu_count", "Number of AMD GPUs visible via amdgpu sysfs.");
	metric_add_auto("amdgpu_gpu_count", &gpu_count, DATATYPE_UINT, carg);
}

static void amdgpu_emit_gauge4(context_arg *carg, const char *name, const char *help,
	double value, const amdgpu_device_snapshot *dev)
{
	amdgpu_metric_set(carg, name, help);
	metric_add_labels4((char *)name, &value, DATATYPE_DOUBLE, carg,
		"name", (char *)dev->name, "index", (char *)dev->index,
		"pci", (char *)dev->pci, "unique_id", (char *)dev->unique_id);
}

void amdgpu_emit_device(context_arg *carg, const amdgpu_device_snapshot *dev)
{
	if (!carg || !dev)
		return;

	uint64_t one = 1;

	if (dev->have & AMDGPU_HAVE_DEVICE_INFO) {
		amdgpu_metric_set(carg, "amdgpu_device_info", "AMD GPU identity labels from amdgpu sysfs.");
		metric_add_labels5("amdgpu_device_info", &one, DATATYPE_UINT, carg,
			"name", (char *)dev->name, "index", (char *)dev->index,
			"pci", (char *)dev->pci, "unique_id", (char *)dev->unique_id,
			"vbios", (char *)dev->vbios);
	}

	if (dev->have & AMDGPU_HAVE_UTIL_GPU)
		amdgpu_emit_gauge4(carg, "amdgpu_utilization_gpu_percent", "GPU busy percent from amdgpu sysfs.",
			dev->util_gpu_percent, dev);
	if (dev->have & AMDGPU_HAVE_UTIL_MEM)
		amdgpu_emit_gauge4(carg, "amdgpu_utilization_memory_percent", "Memory controller busy percent from amdgpu sysfs.",
			dev->util_memory_percent, dev);

	if (dev->have & AMDGPU_HAVE_VRAM) {
		amdgpu_emit_gauge4(carg, "amdgpu_memory_vram_total_bytes", "VRAM size in bytes.",
			dev->vram_total_bytes, dev);
		amdgpu_emit_gauge4(carg, "amdgpu_memory_vram_used_bytes", "VRAM used in bytes.",
			dev->vram_used_bytes, dev);
		amdgpu_emit_gauge4(carg, "amdgpu_memory_vram_free_bytes", "VRAM free in bytes.",
			dev->vram_free_bytes, dev);
	}
	if (dev->have & AMDGPU_HAVE_GTT) {
		amdgpu_emit_gauge4(carg, "amdgpu_memory_gtt_total_bytes", "GTT size in bytes.",
			dev->gtt_total_bytes, dev);
		amdgpu_emit_gauge4(carg, "amdgpu_memory_gtt_used_bytes", "GTT used in bytes.",
			dev->gtt_used_bytes, dev);
	}
	if (dev->have & AMDGPU_HAVE_VIS_VRAM) {
		amdgpu_emit_gauge4(carg, "amdgpu_memory_visible_vram_total_bytes", "CPU-visible VRAM size in bytes.",
			dev->vis_vram_total_bytes, dev);
		amdgpu_emit_gauge4(carg, "amdgpu_memory_visible_vram_used_bytes", "CPU-visible VRAM used in bytes.",
			dev->vis_vram_used_bytes, dev);
	}

	if (dev->have & AMDGPU_HAVE_CLOCK_SCLK)
		amdgpu_emit_gauge4(carg, "amdgpu_clocks_sclk_mhz", "Current shader clock in MHz.",
			dev->clocks_sclk_mhz, dev);
	if (dev->have & AMDGPU_HAVE_CLOCK_MCLK)
		amdgpu_emit_gauge4(carg, "amdgpu_clocks_mclk_mhz", "Current memory clock in MHz.",
			dev->clocks_mclk_mhz, dev);

	if (dev->have & AMDGPU_HAVE_POWER_AVG)
		amdgpu_emit_gauge4(carg, "amdgpu_power_average_watt", "Average GPU power draw in watts.",
			dev->power_average_watt, dev);
	if (dev->have & AMDGPU_HAVE_POWER_CAP)
		amdgpu_emit_gauge4(carg, "amdgpu_power_cap_watt", "GPU power cap in watts.",
			dev->power_cap_watt, dev);

	if (dev->have & AMDGPU_HAVE_FAN)
		amdgpu_emit_gauge4(carg, "amdgpu_fan_speed_rpm", "Fan speed in RPM.",
			dev->fan_speed_rpm, dev);

	for (int i = 0; i < dev->n_temps; ++i) {
		double t = dev->temps[i].celsius;
		amdgpu_metric_set(carg, "amdgpu_temperature_celsius",
			"GPU temperature in Celsius by hwmon sensor label.");
		metric_add_labels5("amdgpu_temperature_celsius", &t, DATATYPE_DOUBLE, carg,
			"name", (char *)dev->name, "index", (char *)dev->index,
			"pci", (char *)dev->pci, "unique_id", (char *)dev->unique_id,
			"sensor", (char *)dev->temps[i].sensor);
	}
}

static int amdgpu_is_card_dir(const char *name)
{
	if (strncmp(name, "card", 4) != 0)
		return 0;
	const char *p = name + 4;
	if (!*p || !isdigit((unsigned char)*p))
		return 0;
	for (; *p; ++p) {
		if (!isdigit((unsigned char)*p))
			return 0;
	}
	return 1;
}

static void amdgpu_trim(char *s)
{
	char *start = s;
	char *end;

	while (*start && isspace((unsigned char)*start))
		start++;
	if (start != s)
		memmove(s, start, strlen(start) + 1);
	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		*--end = '\0';
}

static int amdgpu_streq_ci(const char *a, const char *b)
{
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
		a++;
		b++;
	}
	return *a == *b;
}

static int amdgpu_uevent_get(const char *path, const char *key, char *dst, size_t dstlen)
{
	FILE *fd = fopen(path, "r");
	char line[256];
	size_t klen;

	if (!fd)
		return 0;

	klen = strlen(key);
	while (fgets(line, sizeof(line), fd)) {
		line[strcspn(line, "\r\n")] = 0;
		if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
			strlcpy(dst, line + klen + 1, dstlen);
			fclose(fd);
			return dst[0] ? 1 : 0;
		}
	}
	fclose(fd);
	return 0;
}

static int amdgpu_is_amd_device(const char *devdir)
{
	char path[1024];
	char buf[64];
	char link[256];
	ssize_t n;

	snprintf(path, sizeof(path), "%s/vendor", devdir);
	if (getkvfile_str(path, buf, sizeof(buf))) {
		unsigned long v = strtoul(buf, NULL, 0);
		if (v == 0x1002)
			return 1;
	}

	snprintf(path, sizeof(path), "%s/uevent", devdir);
	if (amdgpu_uevent_get(path, "DRIVER", buf, sizeof(buf)) && !strcmp(buf, "amdgpu"))
		return 1;

	snprintf(path, sizeof(path), "%s/driver", devdir);
	n = readlink(path, link, sizeof(link) - 1);
	if (n > 0) {
		link[n] = '\0';
		const char *base = strrchr(link, '/');
		base = base ? base + 1 : link;
		if (!strcmp(base, "amdgpu"))
			return 1;
	}
	return 0;
}

static int amdgpu_read_u64(const char *path, uint64_t *out)
{
	uint8_t err = 0;
	int64_t v = getkvfile_ext((char *)path, &err);

	if (err)
		return 0;
	if (v < 0)
		return 0;
	*out = (uint64_t)v;
	return 1;
}

static int amdgpu_parse_pp_dpm_mhz(const char *path, double *mhz)
{
	FILE *fd = fopen(path, "r");
	char line[128];
	int found = 0;

	if (!fd)
		return 0;

	while (fgets(line, sizeof(line), fd)) {
		int idx;
		double v;

		if (!strchr(line, '*'))
			continue;
		if (sscanf(line, "%d: %lf", &idx, &v) == 2) {
			*mhz = v;
			found = 1;
			break;
		}
	}
	fclose(fd);
	return found;
}

static void amdgpu_normalize_sensor(char *dst, size_t dstlen, const char *src)
{
	size_t j = 0;

	for (size_t i = 0; src[i] && j + 1 < dstlen; ++i) {
		char c = src[i];
		if (c == ' ' || c == '-' || c == '/')
			c = '_';
		else if (isupper((unsigned char)c))
			c = (char)tolower((unsigned char)c);
		if (c == '_' && j && dst[j - 1] == '_')
			continue;
		dst[j++] = c;
	}
	while (j && dst[j - 1] == '_')
		--j;
	dst[j] = '\0';
	if (!dst[0])
		strlcpy(dst, "unknown", dstlen);
}

static void amdgpu_fill_hwmon(const char *devdir, amdgpu_device_snapshot *snap)
{
	char hwmonroot[1024];
	DIR *dir;
	struct dirent *ent;

	snprintf(hwmonroot, sizeof(hwmonroot), "%s/hwmon", devdir);
	dir = opendir(hwmonroot);
	if (!dir)
		return;

	while ((ent = readdir(dir)) != NULL) {
		char hdir[1100];
		char path[1200];
		char label[64];
		uint64_t raw;
		int i;

		if (ent->d_name[0] == '.')
			continue;

		snprintf(hdir, sizeof(hdir), "%s/%s", hwmonroot, ent->d_name);

		for (i = 1; i <= 16 && snap->n_temps < AMDGPU_SNAP_TEMP_MAX; ++i) {
			snprintf(path, sizeof(path), "%s/temp%d_input", hdir, i);
			if (!amdgpu_read_u64(path, &raw))
				continue;
			snprintf(path, sizeof(path), "%s/temp%d_label", hdir, i);
			if (!getkvfile_str(path, label, sizeof(label)) || !label[0])
				snprintf(label, sizeof(label), "temp%d", i);
			amdgpu_trim(label);
			amdgpu_normalize_sensor(snap->temps[snap->n_temps].sensor,
				sizeof(snap->temps[snap->n_temps].sensor), label);
			snap->temps[snap->n_temps].celsius = (double)raw / 1000.0;
			snap->n_temps++;
		}

		if (!(snap->have & AMDGPU_HAVE_POWER_AVG)) {
			snprintf(path, sizeof(path), "%s/power1_average", hdir);
			if (amdgpu_read_u64(path, &raw)) {
				snap->power_average_watt = (double)raw / 1000000.0;
				snap->have |= AMDGPU_HAVE_POWER_AVG;
			}
		}
		if (!(snap->have & AMDGPU_HAVE_POWER_CAP)) {
			snprintf(path, sizeof(path), "%s/power1_cap", hdir);
			if (amdgpu_read_u64(path, &raw)) {
				snap->power_cap_watt = (double)raw / 1000000.0;
				snap->have |= AMDGPU_HAVE_POWER_CAP;
			}
		}
		if (!(snap->have & AMDGPU_HAVE_FAN)) {
			snprintf(path, sizeof(path), "%s/fan1_input", hdir);
			if (amdgpu_read_u64(path, &raw)) {
				snap->fan_speed_rpm = (double)raw;
				snap->have |= AMDGPU_HAVE_FAN;
			}
		}

		for (i = 1; i <= 4; ++i) {
			snprintf(path, sizeof(path), "%s/freq%d_input", hdir, i);
			if (!amdgpu_read_u64(path, &raw))
				continue;
			snprintf(path, sizeof(path), "%s/freq%d_label", hdir, i);
			if (!getkvfile_str(path, label, sizeof(label)))
				label[0] = '\0';
			amdgpu_trim(label);
			if (!(snap->have & AMDGPU_HAVE_CLOCK_SCLK) &&
			    (!label[0] || amdgpu_streq_ci(label, "sclk"))) {
				snap->clocks_sclk_mhz = (double)raw / 1000000.0;
				snap->have |= AMDGPU_HAVE_CLOCK_SCLK;
				if (!label[0])
					continue;
			}
			if (!(snap->have & AMDGPU_HAVE_CLOCK_MCLK) && amdgpu_streq_ci(label, "mclk")) {
				snap->clocks_mclk_mhz = (double)raw / 1000000.0;
				snap->have |= AMDGPU_HAVE_CLOCK_MCLK;
			}
		}
	}
	closedir(dir);
}

static void amdgpu_fill_device(const char *devdir, const char *cardname, amdgpu_device_snapshot *snap)
{
	char path[1024];
	char buf[128];
	uint64_t raw;

	memset(snap, 0, sizeof(*snap));
	strlcpy(snap->name, "unknown", sizeof(snap->name));
	strlcpy(snap->pci, "unknown", sizeof(snap->pci));
	strlcpy(snap->unique_id, "unknown", sizeof(snap->unique_id));
	strlcpy(snap->vbios, "unknown", sizeof(snap->vbios));
	strlcpy(snap->index, cardname + 4, sizeof(snap->index));

	snprintf(path, sizeof(path), "%s/product_name", devdir);
	if (getkvfile_str(path, buf, sizeof(buf)) && buf[0]) {
		amdgpu_trim(buf);
		strlcpy(snap->name, buf, sizeof(snap->name));
	} else {
		snprintf(path, sizeof(path), "%s/device", devdir);
		if (getkvfile_str(path, buf, sizeof(buf)) && buf[0]) {
			amdgpu_trim(buf);
			snprintf(snap->name, sizeof(snap->name), "AMD %s", buf);
		} else {
			strlcpy(snap->name, "AMD GPU", sizeof(snap->name));
		}
	}

	snprintf(path, sizeof(path), "%s/uevent", devdir);
	if (amdgpu_uevent_get(path, "PCI_SLOT_NAME", buf, sizeof(buf)))
		strlcpy(snap->pci, buf, sizeof(snap->pci));

	snprintf(path, sizeof(path), "%s/unique_id", devdir);
	if (getkvfile_str(path, buf, sizeof(buf)) && buf[0]) {
		amdgpu_trim(buf);
		strlcpy(snap->unique_id, buf, sizeof(snap->unique_id));
	}

	snprintf(path, sizeof(path), "%s/vbios_version", devdir);
	if (getkvfile_str(path, buf, sizeof(buf)) && buf[0]) {
		amdgpu_trim(buf);
		strlcpy(snap->vbios, buf, sizeof(snap->vbios));
	}
	snap->have |= AMDGPU_HAVE_DEVICE_INFO;

	snprintf(path, sizeof(path), "%s/gpu_busy_percent", devdir);
	if (amdgpu_read_u64(path, &raw)) {
		snap->util_gpu_percent = (double)raw;
		snap->have |= AMDGPU_HAVE_UTIL_GPU;
	}
	snprintf(path, sizeof(path), "%s/mem_busy_percent", devdir);
	if (amdgpu_read_u64(path, &raw)) {
		snap->util_memory_percent = (double)raw;
		snap->have |= AMDGPU_HAVE_UTIL_MEM;
	}

	{
		uint64_t total = 0, used = 0;
		int got_t, got_u;

		snprintf(path, sizeof(path), "%s/mem_info_vram_total", devdir);
		got_t = amdgpu_read_u64(path, &total);
		snprintf(path, sizeof(path), "%s/mem_info_vram_used", devdir);
		got_u = amdgpu_read_u64(path, &used);
		if (got_t || got_u) {
			snap->vram_total_bytes = (double)total;
			snap->vram_used_bytes = (double)used;
			snap->vram_free_bytes = got_t && got_u && total >= used ? (double)(total - used) : 0;
			snap->have |= AMDGPU_HAVE_VRAM;
		}

		snprintf(path, sizeof(path), "%s/mem_info_gtt_total", devdir);
		got_t = amdgpu_read_u64(path, &total);
		snprintf(path, sizeof(path), "%s/mem_info_gtt_used", devdir);
		got_u = amdgpu_read_u64(path, &used);
		if (got_t || got_u) {
			snap->gtt_total_bytes = (double)total;
			snap->gtt_used_bytes = (double)used;
			snap->have |= AMDGPU_HAVE_GTT;
		}

		snprintf(path, sizeof(path), "%s/mem_info_vis_vram_total", devdir);
		got_t = amdgpu_read_u64(path, &total);
		snprintf(path, sizeof(path), "%s/mem_info_vis_vram_used", devdir);
		got_u = amdgpu_read_u64(path, &used);
		if (got_t || got_u) {
			snap->vis_vram_total_bytes = (double)total;
			snap->vis_vram_used_bytes = (double)used;
			snap->have |= AMDGPU_HAVE_VIS_VRAM;
		}
	}

	snprintf(path, sizeof(path), "%s/pp_dpm_sclk", devdir);
	if (amdgpu_parse_pp_dpm_mhz(path, &snap->clocks_sclk_mhz))
		snap->have |= AMDGPU_HAVE_CLOCK_SCLK;
	snprintf(path, sizeof(path), "%s/pp_dpm_mclk", devdir);
	if (amdgpu_parse_pp_dpm_mhz(path, &snap->clocks_mclk_mhz))
		snap->have |= AMDGPU_HAVE_CLOCK_MCLK;

	amdgpu_fill_hwmon(devdir, snap);
}

void amdgpu_scrape(void)
{
	char drmroot[1024];
	DIR *dir;
	struct dirent *ent;
	uint64_t count = 0;
	context_arg *carg;

	if (!ac || !ac->system_sysfs || !ac->system_carg)
		return;

	carg = ac->system_carg;
	snprintf(drmroot, sizeof(drmroot), "%s/class/drm", ac->system_sysfs);
	dir = opendir(drmroot);
	if (!dir) {
		carglog(carg, L_DEBUG, "amdgpu: cannot open %s\n", drmroot);
		return;
	}

	while ((ent = readdir(dir)) != NULL) {
		char devdir[1100];
		amdgpu_device_snapshot snap;

		if (!amdgpu_is_card_dir(ent->d_name))
			continue;

		snprintf(devdir, sizeof(devdir), "%s/%s/device", drmroot, ent->d_name);
		if (!amdgpu_is_amd_device(devdir))
			continue;

		amdgpu_fill_device(devdir, ent->d_name, &snap);
		amdgpu_emit_device(carg, &snap);
		count++;
	}
	closedir(dir);

	amdgpu_emit_globals(carg, count);
}
