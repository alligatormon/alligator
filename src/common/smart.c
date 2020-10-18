#ifdef __linux__
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "atasmart.h"
#include "main.h"

static char* print_name(char *s, size_t len, uint8_t id, const char *k) {

	if (k)
		strlcpy(s, k, len+1);
	else
		snprintf(s, len, "%u", id);

	s[len-1] = 0;

	return s;
}

static void disk_dump_attributes(SkDisk *d, const SkSmartAttributeParsedData *a, void* userdata)
{
	extern aconf *ac;
	char *device = userdata;
	char name[32];
	char tt[32], tw[32], tc[32];

	snprintf(tt, sizeof(tt), "%3u", a->threshold);
	tt[sizeof(tt)-1] = 0;
	snprintf(tw, sizeof(tw), "%3u", a->worst_value);
	tw[sizeof(tw)-1] = 0;
	snprintf(tc, sizeof(tc), "%3u", a->current_value);
	tc[sizeof(tc)-1] = 0;

	//char *good_now = a->good_now ? "yes" : "no";
	//char *good_now_valid = a->good_now_valid ? good_now : "n/a";
	//char *good_in_the_past = a->good_in_the_past ? "yes" : "no";
	//char *good_in_the_past_valid = a->good_in_the_past_valid ? good_in_the_past : "n/a";
	print_name(name, sizeof(name), a->id, a->name);

	//printf("field: %3u %-27s %-3s   %-3s   %-3s   %llu 0x%02x%02x%02x%02x%02x%02x %-7s %-7s %-4s %-4s\n",
	//       a->id,
	//       name,
	//       a->current_value_valid ? tc : "n/a",
	//       a->worst_value_valid ? tw : "n/a",
	//       a->threshold_valid ? tt : "n/a",
	//       a->pretty_value,
	//       a->raw[0], a->raw[1], a->raw[2], a->raw[3], a->raw[4], a->raw[5],
	//       a->prefailure ? "prefail" : "old-age",
	//       a->online ? "online" : "offline",
	//       good_now_valid,
	//       good_in_the_past_valid);

	char aid[4];
	snprintf(aid, 4, "%u", a->id);
	int64_t online = a->online;
	int64_t prefailure = a->prefailure;

	int64_t current_value = a->current_value;
	int64_t worst_value = a->worst_value;
	int64_t threshold = a->threshold;
	int64_t pretty_value;
	if (a->pretty_unit == SK_SMART_ATTRIBUTE_UNIT_MKELVIN)
		pretty_value = (a->pretty_value - 273150) / 1000;
	else
		pretty_value = a->pretty_value;

	//puts("smart_attribute_stat_value");
	metric_add_labels3("smart_attribute_stat_value", &current_value, DATATYPE_INT, ac->system_carg, "device", device, "id", aid, "identificator", name);
	//puts("smart_attribute_stat_raw_value");
	metric_add_labels3("smart_attribute_stat_raw_value", &pretty_value, DATATYPE_INT, ac->system_carg, "device", device, "id", aid, "identificator", name);
	//puts("smart_attribute_stat_worst");
	metric_add_labels3("smart_attribute_stat_worst", &worst_value, DATATYPE_INT, ac->system_carg, "device", device, "id", aid, "identificator", name);
	//puts("smart_attribute_stat_threshold");
	metric_add_labels3("smart_attribute_stat_threshold", &threshold, DATATYPE_INT, ac->system_carg, "device", device, "id", aid, "identificator", name);
	//puts("smart_attribute_stat_online");
	metric_add_labels3("smart_attribute_stat_online", &online, DATATYPE_INT, ac->system_carg, "device", device, "id", aid, "identificator", name);
	//puts("smart_attribute_stat_prefailure");
	metric_add_labels3("smart_attribute_stat_prefailure", &prefailure, DATATYPE_INT, ac->system_carg, "device", device, "id", aid, "identificator", name);
	//puts("======");

}

void ata_smart_parsing(SkDisk *d, char *device)
{
	extern aconf *ac;
	SkBool available;
	int64_t vl = 1;
	int ret;
	if ((ret = sk_disk_smart_is_available(d, &available)) < 0)
	{
		// smart not available
		return;
	}

	if ((ret = sk_disk_smart_read_data(d)) < 0)
		return;

	const SkSmartParsedData *spd;
	if ((ret = sk_disk_smart_parse(d, &spd)) < 0)
		return;

	const SkIdentifyParsedData *ipd;

	if ((ret = sk_disk_identify_parse(d, &ipd)) < 0)
		return;

	uint64_t total_offline_data_collection_seconds = spd->total_offline_data_collection_seconds;
	metric_add_labels("smart_total_offline_data_collection_seconds", &total_offline_data_collection_seconds, DATATYPE_UINT, ac->system_carg, "device", device);

	//if (spd->self_test_execution_status)
	//	printf("Self-Test Execution Status: [%s]\n", sk_smart_self_test_execution_status_to_string(spd->self_test_execution_status));

	//printf("Percent Self-Test Remaining: %u%%\n", spd->self_test_execution_percent_remaining);
	uint64_t self_test_execution_percent_remaining = spd->self_test_execution_percent_remaining;
	metric_add_labels("smart_self_test_execution_percent_remaining", &self_test_execution_percent_remaining, DATATYPE_UINT, ac->system_carg, "device", device);

	//if (spd->conveyance_test_available)
	//	printf("Conveyance Self-Test Available: %s\n", spd->conveyance_test_available);

	
	//printf("Short/Extended Self-Test Available: %d\n", spd->short_and_extended_test_available);
	int64_t short_and_extended_test_available = spd->short_and_extended_test_available;
	metric_add_labels("smart_short_and_extended_test_available", &short_and_extended_test_available, DATATYPE_INT, ac->system_carg, "device", device);

	//printf("Start Self-Test Available: %d\n", spd->start_test_available);
	int64_t start_test_available = spd->start_test_available;
	metric_add_labels("smart_start_test_available", &start_test_available, DATATYPE_INT, ac->system_carg, "device", device);

	//printf("Abort Self-Test Available: %d\n", spd->abort_test_available);
	int64_t abort_test_available = spd->abort_test_available;
	metric_add_labels("smart_abort_test_available", &abort_test_available, DATATYPE_INT, ac->system_carg, "device", device);

	//printf("Short Self-Test Polling Time: %u min\n", spd->short_test_polling_minutes);
	uint64_t short_test_polling_minutes = spd->short_test_polling_minutes;
	metric_add_labels("smart_short_test_polling_minutes", &short_test_polling_minutes, DATATYPE_UINT, ac->system_carg, "device", device);

	//printf("Extended Self-Test Polling Time: %u min\n", spd->extended_test_polling_minutes);
	uint64_t extended_test_polling_minutes = spd->extended_test_polling_minutes;
	metric_add_labels("smart_extended_test_polling_minutes", &extended_test_polling_minutes, DATATYPE_UINT, ac->system_carg, "device", device);

	//printf("Conveyance Self-Test Polling Time: %u min\n", spd->conveyance_test_polling_minutes);
	uint64_t conveyance_test_polling_minutes = spd->conveyance_test_polling_minutes;
	metric_add_labels("smart_conveyance_test_polling_minutes", &conveyance_test_polling_minutes, DATATYPE_UINT, ac->system_carg, "device", device);


	// extra info
	uint64_t size;
	uint64_t value, power_on;
	ret = sk_disk_get_size(d, &size);
	if (ret >= 0)
		metric_add_labels("smart_disk_size", &size, DATATYPE_UINT, ac->system_carg, "device", device);
		//printf("Size: %lu B\n", (unsigned long) (size));

	if (sk_disk_smart_get_bad(d, &value) >= 0)
		metric_add_labels("smart_bad_sectors", &value, DATATYPE_UINT, ac->system_carg, "device", device);
		//printf("Bad Sectors: %llu\n", value);

	if (sk_disk_smart_get_power_on(d, &power_on) < 0)
		metric_add_labels("smart_power_on", &power_on, DATATYPE_UINT, ac->system_carg, "device", device);
		//printf("Powered On: %llu\n", power_on);

	if (sk_disk_smart_get_power_cycle(d, &value) >= 0)
	{
		//printf("Power Cycles: %llu\n", (unsigned long long) value);
		metric_add_labels("smart_power_cycles", &value, DATATYPE_UINT, ac->system_carg, "device", device);

		if (value > 0 && power_on > 0)
		{
			uint64_t diff = power_on/value;
			metric_add_labels("smart_average_powered_on_per_power_cycle", &diff, DATATYPE_UINT, ac->system_carg, "device", device);
			//printf("Average Powered On Per Power Cycle: %llu\n", power_on/value);
		}
	}

	if (sk_disk_smart_get_temperature(d, &value) >= 0)
		metric_add_labels("smart_temperature", &value, DATATYPE_UINT, ac->system_carg, "device", device);
		//printf("Temperature: %"PRId64"\n", value);

	SkSmartOverall overall;
	if (sk_disk_smart_get_overall(d, &overall) >= 0)
		metric_add_labels2("smart_overall", &vl, DATATYPE_INT, ac->system_carg, "device", device, "status", (char*)sk_smart_overall_to_string(overall));
		//printf("Overall Status: %s\n", sk_smart_overall_to_string(overall));


	// device parameters
		//printf("Model: [%s]\n"
		//       "Serial: [%s]\n"
		//       "Firmware: [%s]\n",
		//       ipd->model,
		//       ipd->serial,
		//       ipd->firmware);

	metric_add_labels2("smart_model", &vl, DATATYPE_INT, ac->system_carg, "device", device, "model", (char*)ipd->model);
	metric_add_labels2("smart_serial", &vl, DATATYPE_INT, ac->system_carg, "device", device, "serial", (char*)ipd->serial);
	metric_add_labels2("smart_firmware", &vl, DATATYPE_INT, ac->system_carg, "device", device, "firmware", (char*)ipd->firmware);


	// smart parameters
	if ((ret = sk_disk_smart_parse_attributes(d, disk_dump_attributes, device)) < 0)
		return;
}


void get_ata_smart_info(char *device)
{
	int ret;
	SkDisk *d;
	SkBool from_blob = FALSE;

	if (from_blob)
	{
		uint8_t blob[4096];
		size_t size;
		FILE *f = stdin;

		if ((ret = sk_disk_open(NULL, &d)) < 0)
		{
			fprintf(stderr, "Failed to open disk: %s\n", strerror(errno));
			return;
		}

		if (strcmp(device, "-"))
		{
			if (!(f = fopen(device, "r")))
			{
				fprintf(stderr, "Failed to open file: %s\n", strerror(errno));
				if (d)
	       				sk_disk_free(d);
				return;
			}
		}

		size = fread(blob, 1, sizeof(blob), f);

		if (f != stdin)
			fclose(f);

		if (size >= sizeof(blob))
		{
			fprintf(stderr, "File too large for buffer.\n");
			if (d)
	       			sk_disk_free(d);
			return;
		}

		if ((ret = sk_disk_set_blob(d, blob, size)) < 0)
		{
			fprintf(stderr, "Failed to set blob: %s\n", strerror(errno));
			if (d)
	       			sk_disk_free(d);
			return;
		}

	}
	else
	{
		if ((ret = sk_disk_open(device, &d)) < 0)
		{
			fprintf(stderr, "Failed to open disk %s: %s\n", device, strerror(errno));
			return;
		}
	}

	ata_smart_parsing(d, device);
       	if (d)
		sk_disk_free(d);
}
#endif
