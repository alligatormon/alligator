#pragma once
#include <stddef.h>
#include <jansson.h>

struct namespace_struct;
struct labels_t;

typedef struct metric_dump_filter metric_dump_filter;

void metric_dump(int exit_sig);
void metric_restore();

/* Replace ac->persistence_promql from a JSON array of selector strings.
   NULL or an empty array clears the filter (dump every series). */
void persistence_promql_apply(json_t *promql);
void persistence_promql_free(void);

/* Serialize readers and writers of ac->persistence_promql*. */
void persistence_promql_lock(void);
void persistence_promql_unlock(void);

/* Build once per namespace walk. metric_dump_series_selected is what metric_dump_node calls.
   The filter compiles its own copy of the selectors; it does not borrow ac->persistence_promql_mqc. */
metric_dump_filter *metric_dump_filter_build(struct namespace_struct *ns);
void metric_dump_filter_free(metric_dump_filter *filter);
int metric_dump_series_selected(metric_dump_filter *filter, struct labels_t *series);
/* 1 when a selector matcher or its metric name still points into the config list. */
int metric_dump_filter_aliases_config(metric_dump_filter *filter);
