#pragma once
typedef struct config_context
{
	void (*handler)(mtlen*, int64_t*);
	char *key;

	tommy_node node;
} config_context;
void context_aggregate_parser(mtlen *mt, int64_t *i);
void context_entrypoint_parser(mtlen *mt, int64_t *i);
