#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <libgen.h>
#include <limits.h>
#include "system/common.h"
#include "system/linux/nvml.h"
#include "system/linux/dcgm.h"
#include "api/api.h"
extern aconf *ac;
void get_system_metrics();

void test_system_iface_is_veth(void) {
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, system_iface_is_veth("veth9cb223a"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, system_iface_is_veth("veth"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, system_iface_is_veth("vethernet"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, system_iface_is_veth("eth0"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, system_iface_is_veth("docker0"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, system_iface_is_veth("br-abc123"));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, system_iface_is_veth(""));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, system_iface_is_veth(NULL));
}

void test_nvml_emit_metrics(void)
{
	context_arg *carg = calloc(1, sizeof(*carg));
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);

	nvml_emit_globals(carg, 2, "570.86");
	metric_test_run(CMP_EQUAL, "nvml_gpu_count", "nvml_gpu_count", 2);
	metric_test_run(CMP_EQUAL, "nvml_driver_version{version=\"570.86\"}", "nvml_driver_version", 1);

	nvml_device_snapshot snap;
	memset(&snap, 0, sizeof(snap));
	strlcpy(snap.name, "Tesla_T4", sizeof(snap.name));
	strlcpy(snap.uuid, "GPU-test-uuid", sizeof(snap.uuid));
	strlcpy(snap.serial, "0324218021234", sizeof(snap.serial));
	strlcpy(snap.index, "0", sizeof(snap.index));
	strlcpy(snap.pci_bus_id, "00000000:3B:00.0", sizeof(snap.pci_bus_id));
	strlcpy(snap.brand, "Tesla", sizeof(snap.brand));
	strlcpy(snap.pstate, "P0", sizeof(snap.pstate));
	snap.have = NVML_HAVE_DEVICE_INFO | NVML_HAVE_UTIL_GPU | NVML_HAVE_UTIL_MEM |
		NVML_HAVE_MEMORY | NVML_HAVE_TEMP_GPU | NVML_HAVE_POWER_USAGE |
		NVML_HAVE_CLOCK_SM | NVML_HAVE_PSTATE | NVML_HAVE_THROTTLE;
	snap.util_gpu_percent = 42;
	snap.util_memory_percent = 17;
	snap.memory_free_bytes = 1024.0 * 1024.0 * 1024.0;
	snap.memory_used_bytes = 512.0 * 1024.0 * 1024.0;
	snap.memory_total_bytes = 1536.0 * 1024.0 * 1024.0;
	snap.temperature_gpu_celsius = 55;
	snap.power_usage_watt = 70.5;
	snap.clocks_sm_mhz = 1590;
	snap.clocks_throttle_reasons = 0x1; /* GPU idle */

	nvml_emit_device(carg, &snap);

	metric_test_run(CMP_EQUAL,
		"nvml_utilization_gpu_percent{name=\"Tesla_T4\",uuid=\"GPU-test-uuid\",serial=\"0324218021234\",index=\"0\"}",
		"nvml_utilization_gpu_percent", 42);
	metric_test_run(CMP_EQUAL,
		"nvml_memory_used_bytes{name=\"Tesla_T4\",uuid=\"GPU-test-uuid\",serial=\"0324218021234\",index=\"0\"}",
		"nvml_memory_used_bytes", 512.0 * 1024.0 * 1024.0);
	metric_test_run(CMP_EQUAL,
		"nvml_temperature_gpu_celsius{name=\"Tesla_T4\",uuid=\"GPU-test-uuid\",serial=\"0324218021234\",index=\"0\"}",
		"nvml_temperature_gpu_celsius", 55);
	metric_test_run(CMP_EQUAL,
		"nvml_power_usage_watt{name=\"Tesla_T4\",uuid=\"GPU-test-uuid\",serial=\"0324218021234\",index=\"0\"}",
		"nvml_power_usage_watt", 70.5);
	metric_test_run(CMP_EQUAL,
		"nvml_pstate{name=\"Tesla_T4\",uuid=\"GPU-test-uuid\",serial=\"0324218021234\",index=\"0\",pstate=\"P0\"}",
		"nvml_pstate", 1);
	metric_test_run(CMP_EQUAL,
		"nvml_clocks_throttle_gpu_idle{name=\"Tesla_T4\",uuid=\"GPU-test-uuid\",serial=\"0324218021234\",index=\"0\"}",
		"nvml_clocks_throttle_gpu_idle", 1);

	free(carg);
}

void test_nvml_config_enable(void)
{
	int saved = ac->system_nvml;
	ac->system_nvml = 0;
	http_api_v1(NULL, NULL, "{ \"system\": { \"nvml\": {} } }");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, ac->system_nvml);
	ac->system_nvml = saved;
}

void test_dcgm_emit_metrics(void)
{
	context_arg *carg = calloc(1, sizeof(*carg));
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);

	dcgm_emit_globals(carg, 2, 1);
	metric_test_run(CMP_EQUAL, "dcgm_gpu_count", "dcgm_gpu_count", 2);
	metric_test_run(CMP_EQUAL, "dcgm_profiling_available", "dcgm_profiling_available", 1);

	dcgm_device_snapshot snap;
	memset(&snap, 0, sizeof(snap));
	strlcpy(snap.name, "Tesla_T4", sizeof(snap.name));
	strlcpy(snap.uuid, "GPU-dcgm-uuid", sizeof(snap.uuid));
	strlcpy(snap.serial, "0324218021234", sizeof(snap.serial));
	strlcpy(snap.index, "0", sizeof(snap.index));
	strlcpy(snap.pci_bus_id, "00000000:3B:00.0", sizeof(snap.pci_bus_id));
	snap.have = DCGM_HAVE_DEVICE_INFO | DCGM_HAVE_SM_ACTIVE | DCGM_HAVE_SM_OCCUPANCY |
		DCGM_HAVE_TENSOR | DCGM_HAVE_DRAM | DCGM_HAVE_GR_ENGINE;
	snap.gr_engine_active_ratio = 0.25;
	snap.sm_active_ratio = 0.42;
	snap.sm_occupancy_ratio = 0.33;
	snap.tensor_active_ratio = 0.11;
	snap.dram_active_ratio = 0.55;

	dcgm_emit_device(carg, &snap);

	metric_test_run(CMP_EQUAL,
		"dcgm_sm_active_ratio{name=\"Tesla_T4\",uuid=\"GPU-dcgm-uuid\",serial=\"0324218021234\",index=\"0\"}",
		"dcgm_sm_active_ratio", 0.42);
	metric_test_run(CMP_EQUAL,
		"dcgm_dram_active_ratio{name=\"Tesla_T4\",uuid=\"GPU-dcgm-uuid\",serial=\"0324218021234\",index=\"0\"}",
		"dcgm_dram_active_ratio", 0.55);
	metric_test_run(CMP_EQUAL,
		"dcgm_tensor_active_ratio{name=\"Tesla_T4\",uuid=\"GPU-dcgm-uuid\",serial=\"0324218021234\",index=\"0\"}",
		"dcgm_tensor_active_ratio", 0.11);
	metric_test_run(CMP_EQUAL,
		"dcgm_device_info{name=\"Tesla_T4\",uuid=\"GPU-dcgm-uuid\",serial=\"0324218021234\",index=\"0\",pci_bus_id=\"00000000:3B:00.0\"}",
		"dcgm_device_info", 1);

	free(carg);
}

void test_dcgm_config_enable(void)
{
	int saved = ac->system_dcgm;
	ac->system_dcgm = 0;
	http_api_v1(NULL, NULL, "{ \"system\": { \"dcgm\": {} } }");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, ac->system_dcgm);
	ac->system_dcgm = saved;
}

void system_test(char *binary) {
	test_system_iface_is_veth();
	test_nvml_emit_metrics();
	test_nvml_config_enable();
	test_dcgm_emit_metrics();
	test_dcgm_config_enable();
	system_initialize();
    ac->system_procfs = malloc(PATH_MAX + 1);
    ac->system_sysfs = malloc(PATH_MAX + 1);
    ac->system_rundir = malloc(PATH_MAX + 1);
    ac->system_usrdir = malloc(PATH_MAX + 1);
    ac->system_etcdir = malloc(PATH_MAX + 1);

    char *bin_copy = strdup(binary);
    if (!bin_copy) {
        free(ac->system_procfs);
        free(ac->system_sysfs);
        free(ac->system_rundir);
        free(ac->system_usrdir);
        free(ac->system_etcdir);
        ac->system_procfs = ac->system_sysfs = ac->system_rundir = ac->system_usrdir = ac->system_etcdir = NULL;
        return;
    }
    char *pathbin = dirname(bin_copy);
    char *mockpath = malloc(PATH_MAX + 1);
    char *cwd = malloc(PATH_MAX + 1);
    if (!mockpath || !cwd) {
        free(mockpath);
        free(cwd);
        free(bin_copy);
        free(ac->system_procfs);
        free(ac->system_sysfs);
        free(ac->system_rundir);
        free(ac->system_usrdir);
        free(ac->system_etcdir);
        ac->system_procfs = ac->system_sysfs = ac->system_rundir = ac->system_usrdir = ac->system_etcdir = NULL;
        return;
    }
    if (*pathbin == '/') {
        snprintf(mockpath, PATH_MAX, "%s/../tests/mock/linux/", pathbin);
    }
    else {
        if (!getcwd(cwd, PATH_MAX + 1))
            cwd[0] = '\0';
        snprintf(mockpath, PATH_MAX, "%s/%s/../tests/mock/linux/", cwd, pathbin);
    }

    snprintf(ac->system_procfs, PATH_MAX, "%s/proc", mockpath);
    snprintf(ac->system_sysfs, PATH_MAX, "%s/sys",   mockpath);
    snprintf(ac->system_rundir, PATH_MAX, "%s/run",  mockpath);
    snprintf(ac->system_usrdir, PATH_MAX, "%s/usr",  mockpath);
    snprintf(ac->system_etcdir, PATH_MAX, "%s/etc",  mockpath);

	char *config = "{  \"system\": { \
        \"base\": {},\
        \"disk\": {},\
        \"network\": {},\
        \"cadvisor\": {},\
        \"cpuavg\": {\
          \"period\": 5\
        },\
        \"services\": [    ],\
        \"process\": [ \"beam.smp\"  ],\
        \"auditd\": {},\
        \"firewall\": {},\
        \"packages\": []\
      }\
    }";

    http_api_v1(NULL, NULL, config);
    get_system_metrics();
    system_fast_scrape();

#ifdef __linux__
    metric_test_run(CMP_EQUAL, "process_match{name=\"beam.smp\"}", "process_match", 1);
    metric_test_run(CMP_EQUAL, "task_states{state=\"running\"}", "task_states", 1);
    metric_test_run(CMP_EQUAL, "task_states{state=\"uninterruptible\"}", "task_states", 1);
    metric_test_run(CMP_EQUAL, "task_states{state=\"sleeping\"}", "task_states", 5);
    metric_test_run(CMP_EQUAL, "process_states{state=\"sleeping\"}", "process_states", 5);
    metric_test_run(CMP_EQUAL, "process_states{state=\"running\"}", "process_states", 0);
    metric_test_run(CMP_GREATER, "pressure_waiting_seconds_total{resource=\"cpu\"}", "pressure_waiting_seconds_total", 0);
    metric_test_run(CMP_GREATER, "pressure_stalled_seconds_total{resource=\"cpu\"}", "pressure_stalled_seconds_total", 0);
    metric_test_run(CMP_GREATER, "softnet_processed_total{cpu=\"0\"}", "softnet_processed_total", 0);
    metric_test_run(CMP_GREATER, "sockstat_sockets_used", "sockstat_sockets_used", 0);
    metric_test_run(CMP_GREATER, "sockstat_stat_total{protocol=\"TCP\",stat=\"inuse\"}", "sockstat_stat_total", 0);
#else
    metric_test_run(CMP_GREATER, "process_match", "process_match", -1);
#endif
    metric_test_run(CMP_GREATER, "cpu_usage_time", "cpu_usage_time", 0);
    metric_test_run(CMP_GREATER, "cpu_usage_time{type=\"irq\"}", "cpu_usage_time", 0);
    metric_test_run(CMP_GREATER, "cores_num", "cores_num", 0);

    free(cwd);
    free(mockpath);
    free(bin_copy);
}
