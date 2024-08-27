#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/validator.h"
#include "common/logs.h"
#include "main.h"
#define NVIDIA_SMI_MAX_LEN 255
#define NVIDIA_SMI_WIDE_LEN 8192
#define NVIDIA_SMI_METRIC_PREFIX "nvidia_smi_"

void nvidia_smi_handler(char *metrics, size_t size, context_arg *carg)
{
	puts(metrics);
	char *row = metrics;
	char metric_name[NVIDIA_SMI_MAX_LEN];
	size_t metric_prefix_len = strlen(NVIDIA_SMI_METRIC_PREFIX);
	strcpy(metric_name, NVIDIA_SMI_METRIC_PREFIX);
	char columns[255][NVIDIA_SMI_MAX_LEN];
	for (uint64_t i = 0; row - metrics < size; ++i) {
		char gpu_name[NVIDIA_SMI_MAX_LEN] = {0};
		char gpu_uuid[NVIDIA_SMI_MAX_LEN] = {0};
		char gpu_serial[NVIDIA_SMI_MAX_LEN] = {0};
		char dict[NVIDIA_SMI_WIDE_LEN];

		int row_size = strcspn(row, "\n");
		strlcpy(dict, row, row_size+1);
		carglog(carg, L_INFO, "nvidia-smi row is '%s'(%zu)\n", dict, row_size);
		char *field = dict;
		for (uint64_t j = 0; (field - dict < row_size) && (row_size != 0); ++j) {
			char str[NVIDIA_SMI_MAX_LEN];
			int field_size = strcspn(field, ",");
			strlcpy(str, field, field_size+1);
			carglog(carg, L_DEBUG, "parsed str is '%s' (%zu)\n", str, field_size);
			if (!i) {
				char *extrasym = strstr(str, " [");
				if (extrasym)
					extrasym[0] = 0;
				strcpy(columns[j], str);
			}
			else if (strcmp(str, "[N/A]") && strcmp(str, "N/A")) {
				if (!strcmp(columns[j], "uuid"))
					strcpy(gpu_uuid, str);
				else if (!strcmp(columns[j], "name"))
					strcpy(gpu_name, str);
				else if (!strcmp(columns[j], "serial"))
					strcpy(gpu_serial, str);
				else if (!strcmp(columns[j], "timestamp")) {}
				else if (!strcmp(columns[j], "pci.device_id")) {}
				else if (!strcmp(columns[j], "pci.device")) {}
				else if (!strcmp(columns[j], "pci.bus_id")) {}
				else if (!strcmp(columns[j], "pci.sub_device_id")) {}
				else if (!strcmp(columns[j], "compute_mode")) {}
				else if (!strcmp(columns[j], "pci.domain")) {}
				else if (!strcmp(columns[j], "pci.bus")) {}
				else if (strstr(columns[j], "reasons")) {}
				else if (!strcmp(columns[j], "vbios_version")) {
					strcpy(metric_name + metric_prefix_len, columns[j]);
					prometheus_metric_name_normalizer(metric_name, strlen(metric_name));
					metric_label_value_validator_normalizer(str, strlen(str));
					uint64_t val = 1;
					metric_add_labels4(metric_name, &val, DATATYPE_UINT, carg, "name", gpu_name, "uuid", gpu_uuid, "serial", gpu_serial, "version", str);
				}
				else if (!strcmp(columns[j], "driver_version")) {
					strcpy(metric_name + metric_prefix_len, columns[j]);
					prometheus_metric_name_normalizer(metric_name, strlen(metric_name));
					metric_label_value_validator_normalizer(str, strlen(str));
					uint64_t val = 1;
					metric_add_labels(metric_name, &val, DATATYPE_UINT, carg, "version", str);
				}
				else if (!strcmp(columns[j], "pstate")) {
					strcpy(metric_name + metric_prefix_len, columns[j]);
					prometheus_metric_name_normalizer(metric_name, strlen(metric_name));
					metric_label_value_validator_normalizer(str, strlen(str));
					uint64_t val = 1;
					metric_add_labels4(metric_name, &val, DATATYPE_UINT, carg, "name", gpu_name, "uuid", gpu_uuid, "serial", gpu_serial, "pstate", str);
				}
				else if (!strcmp(columns[j], "count")) {
					strcpy(metric_name + metric_prefix_len, columns[j]);
					prometheus_metric_name_normalizer(metric_name, strlen(metric_name));
					uint64_t val = strtoull(str, NULL, 10);
					metric_add_auto(metric_name, &val, DATATYPE_UINT, carg);
				}
				else {
					carglog(carg, L_DEBUG, "[%lu/%lu/%s]: '%s'\n", i, j, columns[j], str);
					strcpy(metric_name + metric_prefix_len, columns[j]);
					prometheus_metric_name_normalizer(metric_name, strlen(metric_name));
					double value;
					if (strstr(str, "Disabled"))
						value = 0;
					else if (strstr(str, "Not Active"))
						value = 0;
					else if (strstr(str, "Enabled"))
						value = 1;
					else
						value = strtod(str, NULL);

					if (strstr(str, "KiB")) {
						value *= 1024;
						strcat(metric_name, "_bytes");
					}
					else if (strstr(str, "MiB")) {
						value *= 1024 * 1024;
						strcat(metric_name, "_bytes");
					}
					else if (strstr(str, "GiB")) {
						value *= 1024 * 1024 * 1024;
						strcat(metric_name, "_bytes");
					}
					else if (strstr(str, " W")) {
						strcat(metric_name, "_watt");
					}
					else if (strstr(str, " %")) {
						strcat(metric_name, "_percent");
					}
					else if (strstr(str, " MHz")) {
						strcat(metric_name, "_mhz");
					}

					//printf("metric '%s' value is '%s' -> %lf\n", metric_name, str, value);
					metric_add_labels3(metric_name, &value, DATATYPE_DOUBLE, carg, "name", gpu_name, "uuid", gpu_uuid, "serial", gpu_serial);
				}
			}

			field += field_size;
			field += strspn(field, ", ");
		}

		row += row_size;
		row += strspn(row, "\n\r");
	}
}

string* nvidia_smi_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("--query-gpu=\"timestamp,driver_version,count,name,serial,uuid,pci.bus_id,pci.domain,pci.bus,pci.device,pci.device_id,pci.sub_device_id,pcie.link.gen.current,pcie.link.gen.max,pcie.link.width.current,pcie.link.width.max,index,display_mode,display_active,persistence_mode,accounting.mode,accounting.buffer_size,driver_model.current,driver_model.pending,vbios_version,inforom.img,inforom.oem,inforom.ecc,inforom.pwr,gom.current,gom.pending,fan.speed,pstate,clocks_throttle_reasons.supported,clocks_throttle_reasons.active,clocks_throttle_reasons.gpu_idle,clocks_throttle_reasons.applications_clocks_setting,clocks_throttle_reasons.sw_power_cap,clocks_throttle_reasons.hw_slowdown,clocks_throttle_reasons.hw_thermal_slowdown,clocks_throttle_reasons.hw_power_brake_slowdown,clocks_throttle_reasons.sw_thermal_slowdown,clocks_throttle_reasons.sync_boost,memory.total,memory.used,memory.free,compute_mode,utilization.gpu,utilization.memory,encoder.stats.sessionCount,encoder.stats.averageFps,encoder.stats.averageLatency,ecc.mode.current,ecc.mode.pending,ecc.errors.corrected.volatile.device_memory,ecc.errors.corrected.volatile.dram,ecc.errors.corrected.volatile.register_file,ecc.errors.corrected.volatile.l1_cache,ecc.errors.corrected.volatile.l2_cache,ecc.errors.corrected.volatile.texture_memory,ecc.errors.corrected.volatile.cbu,ecc.errors.corrected.volatile.sram,ecc.errors.corrected.volatile.total,ecc.errors.corrected.aggregate.device_memory,ecc.errors.corrected.aggregate.dram,ecc.errors.corrected.aggregate.register_file,ecc.errors.corrected.aggregate.l1_cache,ecc.errors.corrected.aggregate.l2_cache,ecc.errors.corrected.aggregate.texture_memory,ecc.errors.corrected.aggregate.cbu,ecc.errors.corrected.aggregate.sram,ecc.errors.corrected.aggregate.total,ecc.errors.uncorrected.volatile.device_memory,ecc.errors.uncorrected.volatile.dram,ecc.errors.uncorrected.volatile.register_file,ecc.errors.uncorrected.volatile.l1_cache,ecc.errors.uncorrected.volatile.l2_cache,ecc.errors.uncorrected.volatile.texture_memory,ecc.errors.uncorrected.volatile.cbu,ecc.errors.uncorrected.volatile.sram,ecc.errors.uncorrected.volatile.total,ecc.errors.uncorrected.aggregate.device_memory,ecc.errors.uncorrected.aggregate.dram,ecc.errors.uncorrected.aggregate.register_file,ecc.errors.uncorrected.aggregate.l1_cache,ecc.errors.uncorrected.aggregate.l2_cache,ecc.errors.uncorrected.aggregate.texture_memory,ecc.errors.uncorrected.aggregate.cbu,ecc.errors.uncorrected.aggregate.sram,ecc.errors.uncorrected.aggregate.total,retired_pages.single_bit_ecc.count,retired_pages.double_bit.count,retired_pages.pending,temperature.gpu,temperature.memory,power.management,power.draw,power.limit,enforced.power.limit,power.default_limit,power.min_limit,power.max_limit,clocks.current.graphics,clocks.current.sm,clocks.current.memory,clocks.current.video,clocks.applications.graphics,clocks.applications.memory,clocks.default_applications.graphics,clocks.default_applications.memory,clocks.max.graphics,clocks.max.sm,clocks.max.memory,mig.mode.current,mig.mode.pending\" --format=csv", 0);
}

void nvidia_smi_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("nvidia_smi");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = nvidia_smi_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = nvidia_smi_mesg;
	strlcpy(actx->handler[0].key,"nvidia_smi", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
