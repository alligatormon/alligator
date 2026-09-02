#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <libgen.h>
#include <limits.h>
#include <fcntl.h>
#include "system/common.h"
#include "system/linux/nvml.h"
#include "system/linux/dcgm.h"
#include "system/linux/amdgpu.h"
#include "system/macosx/gpu.h"
#include "api/api.h"
extern aconf *ac;
void get_system_metrics();
void system_fast_scrape();
void system_slow_scrape();

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

void test_amdgpu_emit_metrics(void)
{
	context_arg *carg = calloc(1, sizeof(*carg));
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);

	amdgpu_emit_globals(carg, 1);
	metric_test_run(CMP_EQUAL, "amdgpu_gpu_count", "amdgpu_gpu_count", 1);

	amdgpu_device_snapshot snap;
	memset(&snap, 0, sizeof(snap));
	strlcpy(snap.name, "Navi_21", sizeof(snap.name));
	strlcpy(snap.index, "0", sizeof(snap.index));
	strlcpy(snap.pci, "0000:3B:00.0", sizeof(snap.pci));
	strlcpy(snap.unique_id, "gpu-test-uid", sizeof(snap.unique_id));
	strlcpy(snap.vbios, "113-TEST", sizeof(snap.vbios));
	snap.have = AMDGPU_HAVE_DEVICE_INFO | AMDGPU_HAVE_UTIL_GPU | AMDGPU_HAVE_UTIL_MEM |
		AMDGPU_HAVE_VRAM | AMDGPU_HAVE_CLOCK_SCLK | AMDGPU_HAVE_POWER_AVG | AMDGPU_HAVE_FAN;
	snap.util_gpu_percent = 37;
	snap.util_memory_percent = 12;
	snap.vram_total_bytes = 17179869184.0;
	snap.vram_used_bytes = 2147483648.0;
	snap.vram_free_bytes = 15032385536.0;
	snap.clocks_sclk_mhz = 1801;
	snap.power_average_watt = 85;
	snap.fan_speed_rpm = 1200;
	snap.n_temps = 1;
	strlcpy(snap.temps[0].sensor, "edge", sizeof(snap.temps[0].sensor));
	snap.temps[0].celsius = 45;

	amdgpu_emit_device(carg, &snap);

	metric_test_run(CMP_EQUAL,
		"amdgpu_utilization_gpu_percent{name=\"Navi_21\",index=\"0\",pci=\"0000:3B:00.0\",unique_id=\"gpu-test-uid\"}",
		"amdgpu_utilization_gpu_percent", 37);
	metric_test_run(CMP_EQUAL,
		"amdgpu_memory_vram_used_bytes{name=\"Navi_21\",index=\"0\",pci=\"0000:3B:00.0\",unique_id=\"gpu-test-uid\"}",
		"amdgpu_memory_vram_used_bytes", 2147483648.0);
	metric_test_run(CMP_EQUAL,
		"amdgpu_power_average_watt{name=\"Navi_21\",index=\"0\",pci=\"0000:3B:00.0\",unique_id=\"gpu-test-uid\"}",
		"amdgpu_power_average_watt", 85);
	metric_test_run(CMP_EQUAL,
		"amdgpu_temperature_celsius{name=\"Navi_21\",index=\"0\",pci=\"0000:3B:00.0\",unique_id=\"gpu-test-uid\",sensor=\"edge\"}",
		"amdgpu_temperature_celsius", 45);
	metric_test_run(CMP_EQUAL,
		"amdgpu_device_info{name=\"Navi_21\",index=\"0\",pci=\"0000:3B:00.0\",unique_id=\"gpu-test-uid\",vbios=\"113-TEST\"}",
		"amdgpu_device_info", 1);

	free(carg);
}

void test_amdgpu_config_enable(void)
{
	int saved = ac->system_amdgpu;
	ac->system_amdgpu = 0;
	http_api_v1(NULL, NULL, "{ \"system\": { \"amdgpu\": {} } }");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, ac->system_amdgpu);
	ac->system_amdgpu = saved;
}

void test_macos_gpu_emit_metrics(void)
{
	context_arg *carg = calloc(1, sizeof(*carg));
	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__, carg);

	macos_gpu_emit_globals(carg, 1);
	metric_test_run(CMP_EQUAL, "macos_gpu_gpu_count", "macos_gpu_gpu_count", 1);

	macos_gpu_device_snapshot snap;
	memset(&snap, 0, sizeof(snap));
	strlcpy(snap.name, "Apple M4 Pro", sizeof(snap.name));
	strlcpy(snap.class_name, "AGXAcceleratorG16X", sizeof(snap.class_name));
	strlcpy(snap.index, "0", sizeof(snap.index));
	strlcpy(snap.compat, "gpu,t6040", sizeof(snap.compat));
	snap.have = MACOS_GPU_HAVE_DEVICE_INFO | MACOS_GPU_HAVE_UTIL_DEVICE |
		MACOS_GPU_HAVE_UTIL_RENDERER | MACOS_GPU_HAVE_UTIL_TILER |
		MACOS_GPU_HAVE_MEM_ALLOC | MACOS_GPU_HAVE_MEM_IN_USE |
		MACOS_GPU_HAVE_CORE_COUNT | MACOS_GPU_HAVE_RECOVERY;
	snap.util_device_percent = 10;
	snap.util_renderer_percent = 10;
	snap.util_tiler_percent = 9;
	snap.memory_alloc_bytes = 7517798400.0;
	snap.memory_in_use_bytes = 1382596608.0;
	snap.core_count = 20;
	snap.recovery_count = 0;

	macos_gpu_emit_device(carg, &snap);

	metric_test_run(CMP_EQUAL,
		"macos_gpu_utilization_device_percent{name=\"Apple M4 Pro\",class=\"AGXAcceleratorG16X\",index=\"0\"}",
		"macos_gpu_utilization_device_percent", 10);
	metric_test_run(CMP_EQUAL,
		"macos_gpu_memory_in_use_bytes{name=\"Apple M4 Pro\",class=\"AGXAcceleratorG16X\",index=\"0\"}",
		"macos_gpu_memory_in_use_bytes", 1382596608.0);
	metric_test_run(CMP_EQUAL,
		"macos_gpu_core_count{name=\"Apple M4 Pro\",class=\"AGXAcceleratorG16X\",index=\"0\"}",
		"macos_gpu_core_count", 20);
	metric_test_run(CMP_EQUAL,
		"macos_gpu_device_info{name=\"Apple M4 Pro\",class=\"AGXAcceleratorG16X\",index=\"0\",compat=\"gpu,t6040\"}",
		"macos_gpu_device_info", 1);

	free(carg);
}

void test_macos_gpu_config_enable(void)
{
	int saved = ac->system_macos_gpu;
	ac->system_macos_gpu = 0;
	http_api_v1(NULL, NULL, "{ \"system\": { \"macos_gpu\": {} } }");
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, ac->system_macos_gpu);
	ac->system_macos_gpu = saved;
}

void system_test(char *binary) {
	test_system_iface_is_veth();
	test_nvml_emit_metrics();
	test_nvml_config_enable();
	test_dcgm_emit_metrics();
	test_dcgm_config_enable();
	test_amdgpu_emit_metrics();
	test_amdgpu_config_enable();
	test_macos_gpu_emit_metrics();
	test_macos_gpu_config_enable();
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
    char mockroot[PATH_MAX + 1];
    mockroot[0] = '\0';
    get_local_directory(mockroot, binary, "tests/mock/linux");
    if (!mockroot[0] || access(mockroot, F_OK) != 0) {
        free(bin_copy);
        free(ac->system_procfs);
        free(ac->system_sysfs);
        free(ac->system_rundir);
        free(ac->system_usrdir);
        free(ac->system_etcdir);
        ac->system_procfs = ac->system_sysfs = ac->system_rundir = ac->system_usrdir = ac->system_etcdir = NULL;
        return;
    }

    snprintf(ac->system_procfs, PATH_MAX, "%s/proc", mockroot);
    snprintf(ac->system_sysfs, PATH_MAX, "%s/sys", mockroot);
    snprintf(ac->system_rundir, PATH_MAX, "%s/run", mockroot);
    snprintf(ac->system_usrdir, PATH_MAX, "%s/usr", mockroot);
    snprintf(ac->system_etcdir, PATH_MAX, "%s/etc", mockroot);

    amdgpu_scrape();
#ifdef __APPLE__
    macos_gpu_scrape();
#endif

	char *config = "{  \"system\": { \
        \"base\": {},\
        \"interrupts\": {},\
        \"memory\": {},\
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
    system_slow_scrape();

#ifdef __linux__
    metric_test_run(CMP_EQUAL, "process_match{name=\"beam.smp\"}", "process_match", 1);
    metric_test_run(CMP_EQUAL, "task_states{state=\"running\"}", "task_states", 1);
    metric_test_run(CMP_EQUAL, "task_states{state=\"uninterruptible\"}", "task_states", 1);
    metric_test_run(CMP_EQUAL, "task_states{state=\"sleeping\"}", "task_states", 5);
    metric_test_run(CMP_EQUAL, "process_states{state=\"sleeping\"}", "process_states", 5);
    metric_test_run(CMP_EQUAL, "process_states{state=\"running\"}", "process_states", 0);
    metric_test_run(CMP_EQUAL, "pressure_waiting_seconds_total{resource=\"cpu\"}", "pressure_waiting_seconds_total", 123456789 / 1000000.0);
    metric_test_run(CMP_EQUAL, "pressure_stalled_seconds_total{resource=\"cpu\"}", "pressure_stalled_seconds_total", 987654321 / 1000000.0);
    metric_test_run(CMP_EQUAL, "softnet_processed_total{cpu=\"0\"}", "softnet_processed_total", 0x123);
    metric_test_run(CMP_EQUAL, "sockstat_sockets_used", "sockstat_sockets_used", 42);
    metric_test_run(CMP_EQUAL, "sockstat_stat_total{protocol=\"TCP\",stat=\"inuse\"}", "sockstat_stat_total", 10);
    metric_test_run(CMP_EQUAL, "swap_device_bytes{device=\"/dev/dm-1\",type=\"size\"}", "swap_device_bytes", 1048572ULL * 1024);
    metric_test_run(CMP_EQUAL, "schedstat_run_periods_total{cpu=\"0\"}", "schedstat_run_periods_total", 3000);
    metric_test_run(CMP_EQUAL, "slabinfo_objects{slab=\"kmalloc-64\",type=\"active\"}", "slabinfo_objects", 100);
    metric_test_run(CMP_EQUAL, "softirq_stat_total{cpu=\"0\",type=\"hi\"}", "softirq_stat_total", 1);
    metric_test_run(CMP_EQUAL, "entropy_available_bits", "entropy_available_bits", 256);
    metric_test_run(CMP_EQUAL, "selinux_enforce_mode", "selinux_enforce_mode", 1);
    metric_test_run(CMP_EQUAL, "bonding_slaves{master=\"bond0\",type=\"total\"}", "bonding_slaves", 2);
    metric_test_run(CMP_EQUAL, "arp_entries{device=\"eth0\"}", "arp_entries", 2);
    metric_test_run(CMP_EQUAL, "ipvs_stat_total{stat=\"connections\"}", "ipvs_stat_total", 42);
    metric_test_run(CMP_EQUAL, "zoneinfo_stat_total{node=\"0\",zone=\"DMA\",stat=\"pages_free\"}", "zoneinfo_stat_total", 100);
    metric_test_run(CMP_EQUAL, "zoneinfo_stat_total{node=\"0\",zone=\"DMA\",stat=\"protection_0\"}", "zoneinfo_stat_total", 0);
    metric_test_run(CMP_EQUAL, "zoneinfo_stat_total{node=\"0\",zone=\"DMA\",stat=\"protection_4\"}", "zoneinfo_stat_total", 7631);
    metric_test_run(CMP_EQUAL, "memory_usage_hw{type=\"total\"}", "memory_usage_hw", 2036900ULL * 1024);
    metric_test_run(CMP_EQUAL, "memory_usage_hw{type=\"usage\"}", "memory_usage_hw", (2036900ULL - 1439972ULL) * 1024);
    metric_test_run(CMP_EQUAL, "numa_node_stat_total{node=\"node0\",stat=\"numa_hit\"}", "numa_node_stat_total", 1000);
#else
    metric_test_run(CMP_GREATER, "process_match", "process_match", -1);
#endif
    metric_test_run(CMP_GREATER, "cpu_usage_time{type=\"user\"}", "cpu_usage_time", 0);
    metric_test_run(CMP_GREATER, "cpu_usage_time{type=\"softirq\"}", "cpu_usage_time", 0);
    metric_test_run(CMP_GREATER, "cpu_usage_time{type=\"idle\"}", "cpu_usage_time", 0);
    metric_test_run(CMP_GREATER, "cores_num", "cores_num", 0);

    metric_test_run(CMP_EQUAL, "amdgpu_gpu_count", "amdgpu_gpu_count", 1);
    metric_test_run(CMP_EQUAL,
        "amdgpu_utilization_gpu_percent{name=\"Radeon RX 6800 XT\",index=\"0\",pci=\"0000:03:00.0\",unique_id=\"0123456789abcdef\"}",
        "amdgpu_utilization_gpu_percent", 37);
    metric_test_run(CMP_EQUAL,
        "amdgpu_memory_vram_used_bytes{name=\"Radeon RX 6800 XT\",index=\"0\",pci=\"0000:03:00.0\",unique_id=\"0123456789abcdef\"}",
        "amdgpu_memory_vram_used_bytes", 2147483648.0);
    metric_test_run(CMP_EQUAL,
        "amdgpu_clocks_sclk_mhz{name=\"Radeon RX 6800 XT\",index=\"0\",pci=\"0000:03:00.0\",unique_id=\"0123456789abcdef\"}",
        "amdgpu_clocks_sclk_mhz", 1801);
    metric_test_run(CMP_EQUAL,
        "amdgpu_power_average_watt{name=\"Radeon RX 6800 XT\",index=\"0\",pci=\"0000:03:00.0\",unique_id=\"0123456789abcdef\"}",
        "amdgpu_power_average_watt", 85);
    metric_test_run(CMP_EQUAL,
        "amdgpu_temperature_celsius{name=\"Radeon RX 6800 XT\",index=\"0\",pci=\"0000:03:00.0\",unique_id=\"0123456789abcdef\",sensor=\"edge\"}",
        "amdgpu_temperature_celsius", 45);
#ifdef __APPLE__
    metric_test_run(CMP_GREATER, "macos_gpu_gpu_count", "macos_gpu_gpu_count", 0);
    metric_test_run(CMP_GREATER, "macos_gpu_core_count", "macos_gpu_core_count", 0);
#endif

    free(bin_copy);
}
