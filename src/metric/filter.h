#pragma once

#include <stddef.h>
#include <jansson.h>
#include "dstructures/ht.h"

struct context_arg;
struct metric_filter;

typedef struct metric_filter metric_filter;

/* Compile metric_filter JSON into runtime rules. Returns NULL if empty/invalid. */
metric_filter *metric_filter_compile(json_t *root);

/* Deep-copy compiled rules (recompiles PCRE). */
metric_filter *metric_filter_dup(const metric_filter *src);

void metric_filter_free(metric_filter *mf);

/* JSON snapshot for config dump (may be NULL). Caller must not decref. */
json_t *metric_filter_json(const metric_filter *mf);

/*
 * Fast path: NULL carg / NULL filter → allow.
 * Semantics (whole-sample): empty → allow;
 *   name include (if any) → must match ≥1;
 *   label include (if any) → sample must match ≥1 include_label rule;
 *   name exclude → drop on match;
 *   label exclude → drop if any sample label matches any exclude_label rule;
 *   order: name include → label include → name exclude → label exclude (excludes win).
 * NULL labels → empty set (label includes fail if any exist; label excludes no-op).
 */
int metric_filter_allow(struct context_arg *carg, const char *name, alligator_ht *labels);

/* Parse "metric_filter" from JSON root into carg (replaces any previous). */
void parse_metric_filter(struct context_arg *carg, json_t *root);
