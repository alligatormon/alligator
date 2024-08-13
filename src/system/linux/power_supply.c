#ifdef __linux__
#include "common/selector.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

void collect_power_supply() {
	char power_supply_dir[1024];
	char fpath[1024];
	char fpath_field[1024];
	char type[256];
	char health[256];
	char status[256];
	char capacity_level[256];
	char metric_name[256];
	snprintf(power_supply_dir, 1023, "%s/class/power_supply", ac->system_sysfs);
	carglog(ac->system_carg, L_INFO, "check power_supply: %s\n", power_supply_dir);
	struct dirent *entry;
	struct dirent *field_entry;
	uint64_t okval = 1;

	DIR *dp = opendir(power_supply_dir);
	if (!dp)
		return;

	while((entry = readdir(dp)))
	{
		if ( entry->d_name[0] == '.' )
			continue;

		*type = 0;

		char dpfield_path[1024];
		snprintf(dpfield_path, 1023, "%s/%s", power_supply_dir, entry->d_name);
		carglog(ac->system_carg, L_DEBUG, "\tcheck power_supply: get dir %s\n", dpfield_path);
		DIR *dp_field = opendir(dpfield_path);
		if (!dp_field)
			return;

		snprintf(fpath, 1023, "%s/%s/type", power_supply_dir, entry->d_name);
		getkvfile_str(fpath, type, 254);
		carglog(ac->system_carg, L_DEBUG, "\tcheck power_supply: set type to %s\n", type);

		snprintf(fpath, 1023, "%s/%s/status", power_supply_dir, entry->d_name);
		if (getkvfile_str(fpath, status, 254))
			metric_add_labels2("power_supply_status", &okval, DATATYPE_UINT, ac->system_carg, "status", status, "device", entry->d_name);

		snprintf(fpath, 1023, "%s/%s/capacity_level", power_supply_dir, entry->d_name);
		if (getkvfile_str(fpath, capacity_level, 254))
			metric_add_labels2("power_supply_capacity_level", &okval, DATATYPE_UINT, ac->system_carg, "capacity_level", capacity_level, "device", entry->d_name);

		snprintf(fpath, 1023, "%s/%s/health", power_supply_dir, entry->d_name);
		if (getkvfile_str(fpath, health, 254))
			metric_add_labels2("power_supply_health", &okval, DATATYPE_UINT, ac->system_carg, "health", health, "device", entry->d_name);

		while((field_entry = readdir(dp_field))) {
			if ( field_entry->d_name[0] == '.' )
				continue;

			char *fname = field_entry->d_name;

			if (strstr(fname, "design")) {}
			else if (
				!strcmp(fname, "type") ||
				!strcmp(fname, "status") ||
				!strcmp(fname, "capacity_level") ||
				strstr(fname, "_min") ||
				strstr(fname, "_max")
				) {
					carglog(ac->system_carg, L_DEBUG, "\tcheck power_supply: skip filename %s\n", fname);
					continue;
			}

			snprintf(fpath_field, 1023, "%s/%s/%s", power_supply_dir, entry->d_name, fname);
			snprintf(metric_name, 255, "power_supply_%s", fname);
			int64_t value = getkvfile(fpath_field);
			carglog(ac->system_carg, L_DEBUG, "\tcheck power_supply: fname is %s (%s, %s, %s) -> '%s' from dir %s, value %ld\n", fname, type, status, capacity_level, metric_name, fpath_field, value);
			metric_add_labels2(metric_name, &value, DATATYPE_INT, ac->system_carg, "type", type, "device", entry->d_name);
		}

		closedir(dp_field);

	}

	closedir(dp);
}
#endif
