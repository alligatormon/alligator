#include "cadvisor/parsers.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int assert_near_double(const char *file, const char *func, int line, double expected, double value)
{
	FILE *out = ut_report ? ut_report : stderr;
	++count_all;
	if (fabs(expected - value) > 0.0000001) {
		++count_error;
		fprintf(out, "%s:%d unit test function '%s' mismatch double: '%f', expected: '%f'\n",
			file, line, func, value, expected);
		fflush(out);
		return do_exit(1);
	}
	return 1;
}

static int cadvisor_numa_count;
static char cadvisor_numa_last_type[32];
static char cadvisor_numa_last_scope[32];
static char cadvisor_numa_last_node[32];
static uint64_t cadvisor_numa_last_pages;

static void test_numa_cb(const char *type, const char *scope, const char *node, uint64_t pages, void *arg)
{
	(void)arg;
	++cadvisor_numa_count;
	strlcpy(cadvisor_numa_last_type, type, sizeof(cadvisor_numa_last_type));
	strlcpy(cadvisor_numa_last_scope, scope, sizeof(cadvisor_numa_last_scope));
	strlcpy(cadvisor_numa_last_node, node, sizeof(cadvisor_numa_last_node));
	cadvisor_numa_last_pages = pages;
}

static int cadvisor_snmp_count;
static char cadvisor_snmp_last_field[64];
static uint64_t cadvisor_snmp_last_val;
static int cadvisor_snmp_last_negative;
static int cadvisor_snmp_maxconn_negative;
static uint64_t cadvisor_snmp_maxconn_val;

static void test_snmp_cb(const char *field, uint64_t val, int negative, void *arg)
{
	(void)arg;
	++cadvisor_snmp_count;
	strlcpy(cadvisor_snmp_last_field, field, sizeof(cadvisor_snmp_last_field));
	cadvisor_snmp_last_val = val;
	cadvisor_snmp_last_negative = negative;
	if (!strcmp(field, "maxconn")) {
		cadvisor_snmp_maxconn_negative = negative;
		cadvisor_snmp_maxconn_val = val;
	}
}

void test_cadvisor_parsers(void)
{
	char kind[16];
	double a10, a60, a300;
	uint64_t total;
	char tmpdir[] = "/tmp/alligator_cadvisor_utXXXXXX";
	char mon_data[256];
	char empty_dir[256];

	char upper[128];

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		cadvisor_overlay_upperdir("lowerdir=/a,upperdir=/var/lib/docker/overlay2/abc/diff,workdir=/w", upper, sizeof(upper)));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "/var/lib/docker/overlay2/abc/diff", upper);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, cadvisor_overlay_upperdir("lowerdir=/a", upper, sizeof(upper)));
	assert_near_double(__FILE__, __FUNCTION__, __LINE__, 2.0, cadvisor_ns_to_seconds(2000000000ULL));
	assert_near_double(__FILE__, __FUNCTION__, __LINE__, 3.0, cadvisor_usec_to_seconds(3000000ULL));
	assert_near_double(__FILE__, __FUNCTION__, __LINE__, 2.5, cadvisor_ticks_to_seconds(250, 100));

	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		cadvisor_parse_pressure_line("some avg10=1.00 avg60=2.00 avg300=3.00 total=123456789",
			kind, sizeof(kind), &a10, &a60, &a300, &total));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "some", kind);
	assert_near_double(__FILE__, __FUNCTION__, __LINE__, 1.0, a10);
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 123456789ULL, total);

	cadvisor_numa_count = 0;
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		cadvisor_parse_numa_stat_line("anon N0=10 N1=20", 1, test_numa_cb, NULL));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 2, cadvisor_numa_count);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "anon", cadvisor_numa_last_type);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "container", cadvisor_numa_last_scope);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "N1", cadvisor_numa_last_node);
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 20ULL, cadvisor_numa_last_pages);

	cadvisor_numa_count = 0;
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		cadvisor_parse_numa_stat_line("total_file N0=5", 0, test_numa_cb, NULL));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "file", cadvisor_numa_last_type);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "hierarchy", cadvisor_numa_last_scope);

	cadvisor_snmp_count = 0;
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		cadvisor_parse_snmp_tcp_pair(
			"Tcp: RtoAlgorithm RtoMin ActiveOpens InSegs",
			"Tcp: 1 200 7 99",
			test_snmp_cb, NULL));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 4, cadvisor_snmp_count);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "insegs", cadvisor_snmp_last_field);
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 99ULL, cadvisor_snmp_last_val);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, cadvisor_snmp_last_negative);

	cadvisor_snmp_count = 0;
	cadvisor_snmp_maxconn_negative = 0;
	cadvisor_snmp_maxconn_val = 0;
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
		cadvisor_parse_snmp_tcp_pair(
			"Tcp: RtoAlgorithm MaxConn InSegs",
			"Tcp: 1 -1 99",
			test_snmp_cb, NULL));
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 3, cadvisor_snmp_count);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, cadvisor_snmp_maxconn_negative);
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 1ULL, cadvisor_snmp_maxconn_val);
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__, "insegs", cadvisor_snmp_last_field);
	assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 99ULL, cadvisor_snmp_last_val);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, cadvisor_snmp_last_negative);

	assert_ptr_notnull(__FILE__, __FUNCTION__, __LINE__,
		(void *)cadvisor_io_cost_metric_name("cost.usage"));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__,
		"container_fs_io_cost_usage_seconds_total",
		cadvisor_io_cost_metric_name("cost.usage"));
	assert_ptr_null(__FILE__, __FUNCTION__, __LINE__,
		(void *)cadvisor_io_cost_metric_name("rbytes"));

	assert_equal_string(__FILE__, __FUNCTION__, __LINE__,
		"container_oom_events_total",
		cadvisor_memory_events_metric_name("oom"));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__,
		"container_memory_oom_kill",
		cadvisor_memory_events_metric_name("oom_kill"));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__,
		"container_memory_events_oom_group_kill_total",
		cadvisor_memory_events_metric_name("oom_group_kill"));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__,
		"container_memory_events_low_total",
		cadvisor_memory_events_metric_name("low"));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__,
		"container_memory_events_high_total",
		cadvisor_memory_events_metric_name("high"));
	assert_equal_string(__FILE__, __FUNCTION__, __LINE__,
		"container_memory_events_max_total",
		cadvisor_memory_events_metric_name("max"));
	assert_ptr_null(__FILE__, __FUNCTION__, __LINE__,
		(void *)cadvisor_memory_events_metric_name("unknown"));

	{
		uint64_t cfg = 0;
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
			cadvisor_parse_perf_event_config("event=0x04,umask=0x03\n", &cfg));
		assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 0x304ULL, cfg);
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1,
			cadvisor_parse_perf_event_config("config=0x1a2", &cfg));
		assert_equal_uint(__FILE__, __FUNCTION__, __LINE__, 0x1a2ULL, cfg);
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0,
			cadvisor_parse_perf_event_config("scale=1.0", &cfg));
	}

	if (mkdtemp(tmpdir)) {
		snprintf(empty_dir, sizeof(empty_dir), "%s/empty", tmpdir);
		mkdir(empty_dir, 0755);
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, cadvisor_dir_has_entries(empty_dir));

		snprintf(mon_data, sizeof(mon_data), "%s/mon_data", tmpdir);
		mkdir(mon_data, 0755);
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, cadvisor_dir_has_entries(mon_data));
		snprintf(mon_data, sizeof(mon_data), "%s/mon_data/mon_L3_00", tmpdir);
		mkdir(mon_data, 0755);
		snprintf(mon_data, sizeof(mon_data), "%s/mon_data", tmpdir);
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, cadvisor_dir_has_entries(mon_data));
	}
}

void test_perf_events_memory_bandwidth_config(void)
{
	int saved_perf = ac->system_perf_events;
	int saved_mb = ac->system_memory_bandwidth;
	ac->system_perf_events = 0;
	ac->system_memory_bandwidth = 0;
	http_api_v1(NULL, NULL, "{ \"system\": { \"perf_events\": {}, \"memory_bandwidth\": {} } }");
#ifdef __linux__
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, ac->system_perf_events);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, ac->system_memory_bandwidth);
#else
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, ac->system_perf_events);
	assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, ac->system_memory_bandwidth);
#endif
	ac->system_perf_events = saved_perf;
	ac->system_memory_bandwidth = saved_mb;

	{
		int saved_cadvisor = ac->system_cadvisor;
		int saved_cperf = ac->cadvisor_perf_events;
		ac->cadvisor_perf_events = 0;
		http_api_v1(NULL, NULL, "{ \"system\": { \"cadvisor\": { \"perf_events\": true } } }");
#ifdef __linux__
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, ac->system_cadvisor);
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 1, ac->cadvisor_perf_events);
#else
		assert_equal_int(__FILE__, __FUNCTION__, __LINE__, 0, ac->cadvisor_perf_events);
#endif
		ac->system_cadvisor = saved_cadvisor;
		ac->cadvisor_perf_events = saved_cperf;
	}
}
