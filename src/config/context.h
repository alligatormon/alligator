#pragma once
#include "dstructures/tommy.h"
#include "common/selector.h"
#define MAPPING_MATCH_GLOB 0
#define MAPPING_MATCH_PCRE 1

typedef struct mapping_metric
{
	char *metric_name;
	char *template;
	size_t template_len;
	struct mapping_metric *next;
	uint64_t match;
	size_t glob_size;
	char **glob;
	size_t wildcard;
	tommy_hashdyn *labels;
} mapping_metric;

typedef struct config_context
{
	void (*handler)(mtlen*, int64_t*);
	char *key;

	tommy_node node;
} config_context;

typedef struct entrypoint_settings
{
	mapping_metric *mm;
} entrypoint_settings;

void context_aggregate_parser(mtlen *mt, int64_t *i);
void context_entrypoint_parser(mtlen *mt, int64_t *i);
void context_system_parser(mtlen *mt, int64_t *i);
char *config_get_arg(mtlen *mt, int64_t i, int64_t num, size_t *out_size);
void push_mapping_metric(mapping_metric *dest, mapping_metric *source);
mapping_metric* context_mapping_parser(mtlen *mt, int64_t *i);
