#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <uv.h>
#include <jansson.h>
#include "main.h"
#include "common/base64.h"
#include "common/selector.h"
#include "common/json_parser.h"
#include "common/http.h"
#include "api/api.h"

void sd_etcd_node(json_t *rnode)
{
	uint64_t i;
	size_t size = json_array_size(rnode);
	for (i = 0; i < size; i++)
	{
		json_t *node = json_array_get(rnode, i);

		json_t *value = json_object_get(node, "value");
		if (value)
		{
			char *dvalue = (char*)json_string_value(value);
			http_api_v1(NULL, NULL, dvalue);
			free(dvalue);
		}

		//json_t *key = json_object_get(node, "key");
		//char *key_str = (char*)json_string_value(key);
		//printf("key is %s\n", key_str);

		json_t *cnode = json_object_get(node, "nodes");
		if (cnode)
			sd_etcd_node(cnode);
	}
}

void sd_etcd_configuration(char *conf, size_t conf_len, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(conf, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	json_t *etcd_node = json_object_get(root, "node");
	json_t *nodes = json_object_get(etcd_node, "nodes");
	sd_etcd_node(nodes);

	json_decref(root);
}

void sd_consul_configuration(char *conf, size_t conf_len, context_arg *carg)
{
	puts(conf);
	json_error_t error;
	json_t *root = json_loads(conf, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	uint64_t i;
	size_t size = json_array_size(root);
	for (i = 0; i < size; i++)
	{
		json_t *node = json_array_get(root, i);

		json_t *value = json_object_get(node, "Value");
		if (value)
		{
			char *value_str = (char*)json_string_value(value);
			size_t outlen;
			char *dvalue = base64_decode(value_str, strlen(value_str), &outlen);
			//printf("consul %s\n", dvalue);
			http_api_v1(NULL, NULL, dvalue);
			free(dvalue);
		}
	}

	json_decref(root);
}

void sd_consul_discovery(char *conf, size_t conf_len, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(conf, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	const char *name;
	json_t *value;
	json_object_foreach(root, name, value)
	{
		if (ac->log_level > 1)
			printf("service id: %s\n", name);
		json_t *meta = json_object_get(value, "Meta");
		if (meta)
		{
			json_t *alligator_handler = json_object_get(meta, "alligator_handler");
			json_t *alligator_url = json_object_get(meta, "alligator_url");
			json_t *alligator_host = json_object_get(meta, "alligator_host");
			json_t *alligator_port = json_object_get(meta, "alligator_port");
			json_t *alligator_proto = json_object_get(meta, "alligator_proto");
			json_t *alligator_path = json_object_get(meta, "alligator_path");

			char *handler = (char*)json_string_value(alligator_handler);
			if (!handler)
				handler = "blackbox";
			char *url = (char*)json_string_value(alligator_url);
			char *host = NULL;
			char *port;
			char *proto;
			char *path;
			char port_c[6];
			char url_c[1024];

			if (!url)
			{
				host = (char*)json_string_value(alligator_host);
				if (!host)
				{
					json_t *address = json_object_get(value, "Address");
					host = (char*)json_string_value(address);
					if (!host)
					{
						if (ac->log_level > 1)
							printf("alligator_host for svc: %s not specified, skip\n", name);
						continue;
					}
				}

				port = (char*)json_string_value(alligator_port);
				if (!port)
				{
					json_t *aport = json_object_get(value, "Port");
					int64_t port_i = json_integer_value(aport);
					if (!port_i)
					{
						if (ac->log_level > 1)
							printf("alligator_port for svc: %s not specified, skip\n", name);
						continue;
					}

					snprintf(port_c, 5, "%"d64, port_i);
					port = port_c;
				}

				proto = (char*)json_string_value(alligator_proto);
				if (!proto)
				{
					if (ac->log_level > 1)
						printf("alligator_proto for svc: %s not specified, skip\n", name);
					continue;
				}

				path = (char*)json_string_value(alligator_path);

				snprintf(url_c, 1023, "%s://%s:%s%s", proto, host, port, path ? path : "");
				url = url_c;
			}

			//printf("handler: %s\n", handler);
			//printf("url: %s\n", url);
			json_t *aggregate_root = json_object();
			json_t *aggregate_arr = json_array();
			json_t *aggregate_obj = json_object();
			json_t *aggregate_handler = json_string(handler);
			json_t *aggregate_url = json_string(url);
			json_t *aggregate_name = json_string(name);
			json_t *aggregate_add_label = json_object();
			json_array_object_insert(aggregate_root, "aggregate", aggregate_arr);
			json_t *aggregate_follow_redirects = json_integer(carg->follow_redirects-1);
			json_array_object_insert(aggregate_arr, "", aggregate_obj);
			json_array_object_insert(aggregate_obj, "handler", aggregate_handler);
			json_array_object_insert(aggregate_obj, "url", aggregate_url);
			json_array_object_insert(aggregate_obj, "add_label", aggregate_add_label);
			json_array_object_insert(aggregate_obj, "follow_redirects", aggregate_follow_redirects);
			json_array_object_insert(aggregate_add_label, "name", aggregate_name);
			char *dvalue = json_dumps(aggregate_root, JSON_INDENT(2));
			http_api_v1(NULL, NULL, dvalue);
			free(dvalue);
		}
	}

	json_decref(root);
}

string* sd_etcd_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	char *replacedquery = malloc(255);
	char *path = "";
	snprintf(replacedquery, 255, "%sv2/keys%s?recursive=true", hi->query, path ? path : "");
	return string_init_add(gen_http_query(0, replacedquery, NULL, hi->host, "alligator", hi->auth, 1, NULL, env, proxy_settings, NULL), 0, 0);
}

void sd_etcd_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("etcd-configuration");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = sd_etcd_configuration;
	actx->handler[0].validator = json_validator;
	actx->handler[0].mesg_func = sd_etcd_mesg;
	strlcpy(actx->handler[0].key,"etcd-configuration", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

string* sd_consul_configuration_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(gen_http_query(0, hi->query, "/v1/kv/?recurse", hi->host, "alligator", hi->auth, 1, "1.0", env, proxy_settings, NULL), 0, 0);
}

void sd_consul_configuration_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("consul-configuration");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = sd_consul_configuration;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = sd_consul_configuration_mesg;
	strlcpy(actx->handler[0].key, "consul-configuration", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

string* sd_consul_discovery_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(gen_http_query(0, hi->query, "/v1/agent/services", hi->host, "alligator", hi->auth, 1, "1.0", env, proxy_settings, NULL), 0, 0);
}

void sd_consul_discovery_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("consul-discovery");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = sd_consul_discovery;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = sd_consul_discovery_mesg;
	strlcpy(actx->handler[0].key, "consul-discovery", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
