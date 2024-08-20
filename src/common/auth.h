#pragma once
#include "dstructures/ht.h"
#include "parsers/http_proto.h"
typedef struct auth_unit {
	char *k;
	tommy_node node;
} auth_unit;
int http_auth_push(alligator_ht *auth_context, char *key);
int8_t http_check_auth(context_arg *carg, http_reply_data *http_data);
alligator_ht *http_auth_copy(alligator_ht *src);
int http_auth_delete(alligator_ht *auth_context, char *key);
void http_auth_foreach_free(void *funcarg, void* arg);
