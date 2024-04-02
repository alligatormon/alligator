#pragma once
#include "events/context_arg.h"
#include "parsers/http_proto.h"
#include "common/selector.h"
void stat_router(string *response, http_reply_data* http_data, context_arg *carg);
void resolver_router(string *response, http_reply_data* http_data, context_arg *carg);
void sharedlock_get_router(string *response, http_reply_data* http_data, context_arg *carg);
void oplog_get_router(string *response, http_reply_data* http_data, context_arg *carg);
void oplog_post_router(string *response, http_reply_data* http_data, context_arg *carg);
void labels_cache_router(string *response, http_reply_data* http_data, context_arg *carg);
void conf_router(string *response, http_reply_data* http_data, context_arg *carg);
void dsv_router(string *response, http_reply_data* http_data, context_arg *carg);
void json_router(string *response, http_reply_data* http_data, context_arg *carg);
void probe_router(string *response, http_reply_data* http_data, context_arg *carg);
void api_router(string *response, http_reply_data* http_data, context_arg *carg);
void sharedlock_post_router(string *response, http_reply_data* http_data, context_arg *carg);
