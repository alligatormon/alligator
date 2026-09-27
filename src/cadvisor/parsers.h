#pragma once
#include <stdint.h>
#include <stddef.h>

double cadvisor_ns_to_seconds(uint64_t ns);
double cadvisor_usec_to_seconds(uint64_t usec);
double cadvisor_ticks_to_seconds(uint64_t ticks, long hz);
long cadvisor_clk_tck(void);

typedef void (*cadvisor_numa_cb)(const char *type, const char *scope, const char *node, uint64_t pages, void *arg);
int cadvisor_parse_numa_stat_line(const char *line, int cgroup_v2, cadvisor_numa_cb cb, void *arg);

int cadvisor_parse_pressure_line(const char *line, char *kind, size_t kindsz,
	double *avg10, double *avg60, double *avg300, uint64_t *total_usec);

/* negative is 1 when the SNMP field was signed (MaxConn is -1). val is then the magnitude. */
typedef void (*cadvisor_snmp_tcp_cb)(const char *field, uint64_t val, int negative, void *arg);
int cadvisor_parse_snmp_tcp_pair(const char *header_line, const char *value_line,
	cadvisor_snmp_tcp_cb cb, void *arg);

/* Returns metric name for an io.stat cost.* key, or NULL if not a cost key. */
const char *cadvisor_io_cost_metric_name(const char *key);

/* Returns metric name for a memory.events key, or NULL if unknown. */
const char *cadvisor_memory_events_metric_name(const char *key);

/* Copy overlay mount upperdir= path into out. Returns 1 on success. */
int cadvisor_overlay_upperdir(const char *opts, char *out, size_t outsz);

/* 1 if path exists and has at least one non-dot entry. */
int cadvisor_dir_has_entries(const char *path);

/* Parse a PMU event file body into perf_event_attr.config. */
int cadvisor_parse_perf_event_config(const char *text, uint64_t *config);
