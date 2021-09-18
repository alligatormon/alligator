#pragma once
#include "dstructures/tommy.h"

typedef struct aggregate_handler {
	void (*name)(char*, size_t, context_arg*);
	int8_t (*validator)(char*, size_t);
	string* (*mesg_func)(host_aggregator_info*, void *arg, void *env, void *proxy_settings);
	uint8_t headers_pass;
	char key[255];
} aggregate_handler;

typedef struct aggregate_context
{
	uint64_t handlers;
	aggregate_handler *handler;
	void *data;
	void* (*data_func)(host_aggregator_info*, void *arg, void *data);

	tommy_node node;
	char *key;
} aggregate_context;

int actx_compare(const void* arg, const void* obj);
int aggregator_compare(const void* arg, const void* obj);
void try_again(context_arg *carg, char *mesg, size_t mesg_len, void *handler, char *parser_name, void *validator, char *override_key, void *data);
context_arg *aggregator_oneshot(context_arg *carg, char *url, size_t url_len, char *mesg, size_t mesg_len, void *handler, char *parser_name, void *validator, char *override_key, uint64_t follow_redirects, void *data, char *s_stdin, size_t l_stdin);
