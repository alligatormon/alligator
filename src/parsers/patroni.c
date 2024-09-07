#include <stdio.h>
#include <inttypes.h>
#include <jansson.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/json_query.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "main.h"

//{
//  "state": "running",
//  "cluster_unlocked": false,
//  "xlog": {
//    "location": 67109184
//  },
//  "patroni": {
//    "version": "1.5.5",
//    "scope": "demo"
//  },
//  "role": "master",
//  "database_system_identifier": "6899348851414573091",
//  "server_version": 100007,
//  "timeline": 1,
//  "postmaster_start_time": "2020-11-26 08:37:15.182 UTC",
//  "replication": [
//    {
//      "sync_state": "async",
//      "sync_priority": 0,
//      "state": "streaming",
//      "client_addr": "172.18.0.2",
//      "usename": "replicator",
//      "application_name": "patroni3"
//    },
//    {
//      "sync_state": "async",
//      "sync_priority": 0,
//      "state": "streaming",
//      "client_addr": "172.18.0.8",
//      "usename": "replicator",
//      "application_name": "patroni1"
//    }
//  ]
//}

void patroni_string_to_label(json_t *root, char *str, context_arg *carg)
{
	json_t *jstr = json_object_get(root, str);
	char *stat_str = (char*)json_string_value(jstr);
	uint64_t vl = 1;
	char metric_name[255];
	snprintf(metric_name, 254, "patroni_%s", str);
	if (stat_str)
		metric_add_labels(metric_name, &vl, DATATYPE_UINT, carg, str, stat_str);
	carg->parser_status = 1;
}

void patroni_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	patroni_string_to_label(root, "state", carg);
	patroni_string_to_label(root, "role", carg);

	json_t *jcluster_unlocked = json_object_get(root, "cluster_unlocked");
	int jsontype = json_typeof(jcluster_unlocked);
	uint64_t vl;
	if (jsontype == JSON_TRUE)
		vl = 1;
	else if (jsontype == JSON_FALSE)
		vl = 0;
	metric_add_auto("patroni_cluster_unlocked", &vl, DATATYPE_UINT, carg);

	json_t *jxlog = json_object_get(root, "xlog");
	if (jxlog)
	{
		json_t *jlocation = json_object_get(jxlog, "location");
		int64_t location = json_integer_value(jlocation);
		metric_add_auto("patroni_xlog_location", &location, DATATYPE_INT, carg);
	}

	json_t *jreplication = json_object_get(root, "replication");
	size_t replication_size = json_array_size(jreplication);

	for (uint64_t i = 0; i < replication_size; i++)
	{
		json_t *replicate = json_array_get(jreplication, i);

		json_t *jsync_state = json_object_get(replicate, "sync_state");
		char *sync_state = (char*)json_string_value(jsync_state);

		json_t *jstate = json_object_get(replicate, "state");
		char *state = (char*)json_string_value(jstate);

		json_t *jusename = json_object_get(replicate, "usename");
		char *usename = (char*)json_string_value(jusename);

		json_t *jclient_addr = json_object_get(replicate, "client_addr");
		char *client_addr = (char*)json_string_value(jclient_addr);

		json_t *japplication_name = json_object_get(replicate, "application_name");
		char *application_name = (char*)json_string_value(japplication_name);

		json_t *jsync_priority = json_object_get(replicate, "sync_priority");
		int64_t sync_priority = json_integer_value(jsync_priority);

		metric_add_labels5("patroni_replication_sync_priority", &sync_priority, DATATYPE_INT, carg, "sync_state", sync_state, "state", state, "usename", usename, "client_addr", client_addr, "application_name", application_name);
	}

	json_decref(root);
	carg->parser_status = 1;
}

void patroni_config_handler(char *metrics, size_t size, context_arg *carg)
{
	carg->parser_status = json_query(metrics, NULL, "patroni_settings", carg, carg->pquery, carg->pquery_size);
}

string *patroni_gen_url(host_aggregator_info *hi, char *addition, void *env, void *proxy_settings)
{
	return string_init_add_auto(gen_http_query(0, hi->query, addition, hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
}

string* patroni_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return patroni_gen_url(hi, "/patroni", env, proxy_settings); }
string* patroni_config_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings) { return patroni_gen_url(hi, "/config", env, proxy_settings); }

void patroni_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("patroni");
	actx->handlers = 2;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = patroni_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = patroni_mesg;
	strlcpy(actx->handler[0].key,"patroni", 255);

	actx->handler[1].name = patroni_config_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = patroni_config_mesg;
	strlcpy(actx->handler[1].key,"patroni_config", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
