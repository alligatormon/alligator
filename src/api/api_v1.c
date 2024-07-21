#include <jansson.h>
#include "main.h"
#include "parsers/http_proto.h"
#include "config/mapping.h"
#include "common/url.h"
#include "common/http.h"
#include "common/pem_check.h"
#include "common/selector.h"
#include "cadvisor/run.h"
#include "lang/lang.h"
#include "query/query.h"
#include "common/netlib.h"
#include "probe/probe.h"
#include "scheduler/type.h"
#include "common/mkdirp.h"
#include "common/reject.h"
#include "parsers/multiparser.h"
#include "config/mapping.h"
#include "action/action.h"
#include "events/context_arg.h"
#include "events/server.h"
#include "events/udp.h"
#include "events/unix_server.h"
#include "events/unixgram.h"
#include "common/base64.h"
#include "cluster/type.h"
#include "system/common.h"
#include "common/json_parser.h"
#include "common/logs.h"
#include "puppeteer/puppeteer.h"
#include "common/units.h"
#define DOCKERSOCK "http://unix:/var/run/docker.sock:/containers/json"

uint16_t http_error_handler_v1(int8_t ret, char *mesg_good, char *mesg_fail, const char *proto, const char *address, uint16_t port, char *status, char* respbody)
{
	uint16_t code = 200;
	if (!ret)
	{
		snprintf(status, 100, "Bad Request");
		snprintf(respbody, 1000, "%s context with proto '%s', address '%s' and port '%"u16"\n", mesg_fail, proto, address, port);
		code = 400;
	}
	else
	{
		snprintf(status, 100, "Created");
		snprintf(respbody, 1000, "%s context with proto '%s', address '%s' and port '%"u16"\n", mesg_good, proto, address, port);
		code = 200;
	}
	return code;
}

void http_api_v1(string *response, http_reply_data* http_data, const char *configbody)
{
	extern aconf *ac;
	const char *body = http_data ? http_data->body : configbody;
	uint8_t method = http_data ? http_data->method : HTTP_METHOD_PUT;
	uint16_t code = 200;
	char temp_resp[1200];
	char respbody[1000];
	char status[100];
	snprintf(status, 100, "OK");
	//printf("http_api_v1 is %s\n", body);

	json_error_t error;
	json_t *root = json_loads(body, 0, &error);
	if (!root)
	{
		code = 400;
		snprintf(status, 100, "Bad Request");
		snprintf(respbody, 1000, "json error on line %d: %s\n", error.line, error.text);
		if (ac->log_level > 0)
			fprintf(stderr, "%s", respbody);
	}

	if (root)
	{
		const char *key;
		json_t *value;
		json_object_foreach(root, key, value)
		{
			if (!strcmp(key, "log_level"))
			{
				if (json_typeof(value) == JSON_STRING) {
					ac->log_level = get_log_level_by_name(json_string_value(value), json_string_length(value));
					if (!ac->system_carg->log_level)
						ac->system_carg->log_level = ac->log_level;
				}
				else {
					ac->log_level = json_integer_value(value);
					if (!ac->system_carg->log_level)
						ac->system_carg->log_level = ac->log_level;
				}
			}
			if (!strcmp(key, "log_dest"))
			{
				if (ac->log_dest)
					free(ac->log_dest);
				ac->log_dest = strdup(json_string_value(value));
				ac->update_log_dest = 1;
			}
			if (!strcmp(key, "log_form"))
			{
				const char *form = json_string_value(value);
				ac->log_form = FORM_SIMPLE;
				if (!strcmp(form, "syslog")) {
					ac->log_form = FORM_SYSLOG;
				}
			}
			if (!strcmp(key, "aggregate_period"))
			{
				uint64_t aggregator_repeat;
				if (json_typeof(value) == JSON_STRING)
					aggregator_repeat = get_ms_from_human_range(json_string_value(value), json_string_length(value));
				else
					aggregator_repeat = json_integer_value(value);

				ac->aggregator_repeat = ac->iggregator_repeat = ac->unixgram_aggregator_repeat = ac->file_aggregator_repeat = aggregator_repeat;
			}
			if (!strcmp(key, "system_collect_period"))
			{
				uint64_t aggregator_repeat;
				if (json_typeof(value) == JSON_STRING)
					aggregator_repeat = get_ms_from_human_range(json_string_value(value), json_string_length(value));
				else
					aggregator_repeat = json_integer_value(value);

				ac->system_aggregator_repeat = aggregator_repeat;
			}
			if (!strcmp(key, "tls_collect_period"))
			{
				uint64_t aggregator_repeat;
				if (json_typeof(value) == JSON_STRING)
					aggregator_repeat = get_ms_from_human_range(json_string_value(value), json_string_length(value));
				else
					aggregator_repeat = json_integer_value(value);

				ac->tls_fs_repeat = aggregator_repeat;
			}
			if (!strcmp(key, "query_period"))
			{
				uint64_t query_repeat;
				if (json_typeof(value) == JSON_STRING)
					query_repeat = get_ms_from_human_range(json_string_value(value), json_string_length(value));
				else
					query_repeat = json_integer_value(value);

				ac->query_repeat = query_repeat;
			}
			if (!strcmp(key, "synchronization_period"))
			{
				uint64_t cluster_repeat;
				if (json_typeof(value) == JSON_STRING)
					cluster_repeat = get_ms_from_human_range(json_string_value(value), json_string_length(value));
				else
					cluster_repeat = json_integer_value(value);

				ac->cluster_repeat = cluster_repeat;
			}
			if (!strcmp(key, "ttl"))
			{
				if (json_typeof(value) == JSON_STRING)
					ac->ttl = get_sec_from_human_range(json_string_value(value), json_string_length(value));
				else
					ac->ttl = json_integer_value(value);
			}
			if (!strcmp(key, "modules"))
			{
				json_t *module_path;
				const char *module_key;
				json_object_foreach(value, module_key, module_path)
				{
					if (method == HTTP_METHOD_DELETE)
					{
						module_t *module = alligator_ht_search(ac->modules, module_compare, module_key, tommy_strhash_u32(0, module_key));
						free(module->key);
						free(module->path);
						alligator_ht_remove_existing(ac->modules, &(module->node));

						free(module);
						continue;
					}

					char *path = (char*)json_string_value(module_path);

					module_t *module = calloc(1, sizeof(*module));
					module->key = strdup(module_key);
					module->path = strdup(path);

					alligator_ht_insert(ac->modules, &(module->node), module, tommy_strhash_u32(0, module->key));
				}
			}
			if (!strcmp(key, "x509"))
			{
				uint64_t x509_size = json_array_size(value);
				for (uint64_t i = 0; i < x509_size; i++)
				{
					json_t *x509 = json_array_get(value, i);
					json_t *jname = json_object_get(x509, "name");
					if (!jname)
						continue;
					char *name = (char*)json_string_value(jname);

					json_t *jpath = json_object_get(x509, "path");
					if (!jpath)
						continue;
					char *path = (char*)json_string_value(jpath);

					json_t *jmatch = json_object_get(x509, "match");
					if (!jmatch)
						continue;
					char *match = (char*)json_string_value(jmatch);

					json_t *jpassword = json_object_get(x509, "password");
					char *password = (char*)json_string_value(jpassword);

					json_t *jtype = json_object_get(x509, "type");
					char *type = (char*)json_string_value(jtype);

					uint64_t period = 10000;
					json_t *json_period = json_object_get(x509, "period");
					if (json_period)
						period = get_ms_from_human_range(json_string_value(json_period), json_string_length(json_period));

					if (method == HTTP_METHOD_DELETE)
					{
						if (!strcmp(type, "jks"))
							jks_del(name);
						else
							tls_fs_del(name);
						continue;
					}

					if (type && !strcmp(type, "jks"))
						jks_push(name, path, match, password, NULL, period);
					else
						tls_fs_push(name, path, match, password, type, period);
				}
			}
			if (!strcmp(key, "query"))
			{
				uint64_t x509_size = json_array_size(value);
				for (uint64_t i = 0; i < x509_size; i++)
				{
					json_t *x509 = json_array_get(value, i);
					json_t *jmake = json_object_get(x509, "make");
					if (!jmake)
						continue;
					char *make = (char*)json_string_value(jmake);

					json_t *jexpr = json_object_get(x509, "expr");
					if (!jexpr)
						continue;
					char *expr = (char*)json_string_value(jexpr);

					json_t *jaction = json_object_get(x509, "action");
					char *action = (char*)json_string_value(jaction);

					json_t *jdatasource = json_object_get(x509, "datasource");
					if (!jdatasource)
						continue;
					char *datasource = (char*)json_string_value(jdatasource);

					json_t *jns = json_object_get(x509, "ns");
					char *ns = (char*)json_string_value(jns);

					json_t *jfield = json_object_get(x509, "field");

					if (method == HTTP_METHOD_DELETE)
					{
						query_del(datasource, make);
						continue;
					}

					query_push(datasource, expr, make, action, ns, jfield);
				}
			}
			if (!strcmp(key, "action"))
			{
				uint64_t action_size = json_array_size(value);
				for (uint64_t i = 0; i < action_size; i++)
				{
					json_t *action = json_array_get(value, i);
					if (method == HTTP_METHOD_DELETE)
					{
						action_del(action);
						continue;
					}

					action_push(action);
				}
			}
			if (!strcmp(key, "probe"))
			{
				uint64_t probe_size = json_array_size(value);
				for (uint64_t i = 0; i < probe_size; i++)
				{
					json_t *probe = json_array_get(value, i);

					if (method == HTTP_METHOD_DELETE)
					{
						probe_del_json(probe);
						continue;
					}

					probe_push_json(probe);
				}
			}
			if (!strcmp(key, "resolver"))
			{
				uint64_t resolver_size = json_array_size(value);
				for (uint64_t i = 0; i < resolver_size; i++)
				{
					json_t *resolver = json_array_get(value, i);

					if (method == HTTP_METHOD_DELETE)
					{
						resolver_del_json(resolver);
						continue;
					}

					resolver_push_json(resolver);
				}
			}
			if (!strcmp(key, "cluster"))
			{
				uint64_t cluster_size = json_array_size(value);
				for (uint64_t i = 0; i < cluster_size; i++)
				{
					json_t *cluster = json_array_get(value, i);

					if (method == HTTP_METHOD_DELETE)
					{
						cluster_del_json(cluster);
						continue;
					}

					cluster_push_json(cluster);
				}
			}
			if (!strcmp(key, "scheduler"))
			{
				uint64_t scheduler_size = json_array_size(value);
				for (uint64_t i = 0; i < scheduler_size; i++)
				{
					json_t *scheduler = json_array_get(value, i);

					if (method == HTTP_METHOD_DELETE)
					{
						scheduler_del_json(scheduler);
						continue;
					}

					scheduler_push_json(scheduler);
				}
			}
			if (!strcmp(key, "persistence"))
			{
				if (method == HTTP_METHOD_DELETE)
				{
					if (ac->persistence_dir)
					{
						free(ac->persistence_dir);
						ac->persistence_dir = NULL;
					}
					continue;
				}

				json_t *directory = json_object_get(value, "directory");
				if (directory && (json_typeof(directory) == JSON_STRING))
					ac->persistence_dir = strdup(json_string_value(directory));
				else
					ac->persistence_dir = strdup("/var/lib/alligator");

				mkdirp(ac->persistence_dir);

				json_t *period = json_object_get(value, "period");
				if (period)
					ac->persistence_period = json_integer_value(period)*1000;
				else
					ac->persistence_period = 300;
					
			}
			if (!strcmp(key, "lang"))
			{
				uint64_t lang_size = json_array_size(value);
				for (uint64_t i = 0; i < lang_size; i++)
				{
					json_t *lang = json_array_get(value, i);

					json_t *lang_key = json_object_get(lang, "key");
					if (!lang_key)
					{
						if (ac->log_level)
							printf("no key for lang ??\n");
						continue;
					}
					char *key = (char*)json_string_value(lang_key);

					if (method == HTTP_METHOD_DELETE)
					{
						lang_delete(key);
						continue;
					}

					lang_push(lang, key);
				}
			}
			if (!strcmp(key, "entrypoint"))
			{
				//if (method == HTTP_METHOD_DELETE)
				//{
				//	char *port = strstr(tcp_string, ":");
				//	if (port)
				//	{
				//		char *host = strndup(tcp_string, port - tcp_string);
				//	tls_server_delete(ac->loop, host, port, proto);
				//	continue;
				//}
				uint64_t entrypoint_size = json_array_size(value);
				for (uint64_t i = 0; i < entrypoint_size; i++)
				{
					json_t *entrypoint = json_array_get(value, i);

					context_arg *carg = calloc(1, sizeof(*carg)); // TODO: memory leak
					carg->buffer_request_size = 6553500;
					carg->buffer_response_size = 6553500;

					json_t *json_auth_header = json_object_get(entrypoint, "auth_header");
					if (json_auth_header)
						carg->auth_header = strdup((char*)json_string_value(json_auth_header));
					else
						carg->auth_header = strdup("Authorization");

					carg->ttl = -1;
					json_t *carg_ttl = json_object_get(entrypoint, "ttl");
					if (carg_ttl) {
						int carg_ttl_type = json_typeof(carg_ttl);
						if (carg_ttl_type == JSON_STRING)
							carg->ttl = get_sec_from_human_range(json_string_value(carg_ttl), json_string_length(carg_ttl));
						else if (carg_ttl_type == JSON_REAL)
							carg->ttl = json_real_value(carg_ttl);
						else if (carg_ttl_type == JSON_INTEGER)
							carg->ttl = json_real_value(carg_ttl);
					}

					json_t *carg_log_level = json_object_get(entrypoint, "log_level");
					if (carg_log_level) {
						int carg_log_level_type = json_typeof(carg_log_level);
						if (carg_log_level_type == JSON_STRING)
							carg->log_level = get_log_level_by_name(json_string_value(carg_log_level), json_string_length(carg_log_level));
						else if (carg_log_level_type == JSON_REAL)
							carg->log_level = json_real_value(carg_log_level);
						else if (carg_log_level_type == JSON_INTEGER)
							carg->log_level = json_real_value(carg_log_level);
					}

					json_t *carg_api = json_object_get(entrypoint, "api");
					char *api = (char*)json_string_value(carg_api);
					if (api && !strcmp(api, "on"))
						carg->api_enable = 1;
					else
						carg->api_enable = 0;

					carg->pingloop = 1;
					json_t *carg_pingloop = json_object_get(entrypoint, "pingloop");
					uint64_t pingloop = json_integer_value(carg_pingloop);
					if (pingloop)
						carg->pingloop = pingloop;

					json_t *carg_handler = json_object_get(entrypoint, "handler");
					char *str_handler = (char*)json_string_value(carg_handler);
					if (str_handler && !strcmp(str_handler, "rsyslog-impstats"))
						carg->parser_handler =  &rsyslog_impstats_handler;
					else if (str_handler && !strcmp(str_handler, "lang"))
						carg->parser_handler =  &lang_parser_handler;
					else if (str_handler && !strcmp(str_handler, "log"))
						carg->parser_handler =  &log_handler;

					carg->env = env_struct_parser(entrypoint);

					json_t *allow = json_object_get(entrypoint, "allow");
					if (allow)
					{
						uint64_t allow_size = json_array_size(allow);
						for (uint64_t i = 0; i < allow_size; i++)
						{
							json_t *allow_obj = json_array_get(allow, i);
							char *allow_str = (char*)json_string_value(allow_obj);

							if (method == HTTP_METHOD_DELETE)
								network_range_delete(carg->net_tree_acl, carg->net6_tree_acl, allow_str);
							else
								network_range_push(&carg->net_tree_acl, &carg->net6_tree_acl, allow_str, IPACCESS_ALLOW);
						}
					}
					json_t *deny = json_object_get(entrypoint, "deny");
					if (deny)
					{
						uint64_t deny_size = json_array_size(deny);
						for (uint64_t i = 0; i < deny_size; i++)
						{
							json_t *deny_obj = json_array_get(deny, i);
							char *deny_str = (char*)json_string_value(deny_obj);

							if (method == HTTP_METHOD_DELETE)
								network_range_delete(carg->net_tree_acl, carg->net6_tree_acl, deny_str);
							else
								network_range_push(&carg->net_tree_acl, &carg->net6_tree_acl, deny_str, IPACCESS_DENY);
						}
					}

					json_t *reject = json_object_get(entrypoint, "reject");
					if (reject)
					{
						if (!carg->reject)
						{
							carg->reject = calloc(1, sizeof(*carg->reject));
							alligator_ht_init(carg->reject);
						}

						uint64_t reject_size = json_array_size(reject);
						for (uint64_t i = 0; i < reject_size; i++)
						{
							json_t *reject_obj = json_array_get(reject, i);

							const char *reject_key;
							json_t *reject_value;
							json_object_foreach(reject_obj, reject_key, reject_value)
							{
								if (method == HTTP_METHOD_DELETE)
									reject_delete(carg->reject, strdup(reject_key));
								else
								{
									char *reject_value_str = (char*)json_string_value(reject_value);
									char *reject_value_normalized = reject_value_str ? strdup(reject_value_str) : NULL;
									reject_insert(carg->reject, strdup(reject_key), reject_value_normalized);
								}
							}
						}
					}
					json_t *mapping = json_object_get(entrypoint, "mapping");
					if (mapping)
					{
						uint64_t mapping_size = json_array_size(mapping);
						for (uint64_t i = 0; i < mapping_size; i++)
						{
							json_t *mapping_obj = json_array_get(mapping, i);
							mapping_metric *mm = json_mapping_parser(mapping_obj);

							if (!carg->mm)
								carg->mm = mm;
							else
								push_mapping_metric(carg->mm, mm);
						}
					}

					json_t *json_cluster = json_object_get(entrypoint, "cluster");
					if (json_cluster)
					{
						carg->cluster = strdup(json_string_value(json_cluster));
						carg->cluster_size = json_string_length(json_cluster);
					}

					json_t *json_instance = json_object_get(entrypoint, "instance");
					if (json_instance)
					{
						carg->instance = strdup(json_string_value(json_instance));
					}

					json_t *json_return = json_object_get(entrypoint, "return");
					if (json_return)
					{
						char *str_return = (char*)json_string_value(json_return);
						if (!strcmp(str_return, "empty"))
							carg->rreturn = ENTRYPOINT_RETURN_EMPTY;
					}

					json_t *json_lang = json_object_get(entrypoint, "lang");
					if (json_lang)
					{
						carg->lang = strdup(json_string_value(json_lang));
					}

					json_t *json_aggregation = json_object_get(entrypoint, "metric_aggregation");
					if (json_aggregation)
					{
						const char *aggregation = json_string_value(json_aggregation);
						if (!strcmp(aggregation, "count"))
						{
							carg->metric_aggregation = ENTRYPOINT_AGGREGATION_COUNT;
						}
					}

					json_t *json_auth = json_object_get(entrypoint, "auth");
					if (json_auth)
					{
						uint64_t auth_size = json_array_size(json_auth);

						for (uint64_t i = 0; i < auth_size; i++)
						{
							json_t *auth_obj = json_array_get(json_auth, i);

							json_t *json_auth_type = json_object_get(auth_obj, "type");
							char *auth_type = (char*)json_string_value(json_auth_type);

							json_t *json_auth_data = json_object_get(auth_obj, "data");
							char *auth_data = (char*)json_string_value(json_auth_data);
							size_t auth_data_len = json_string_length(json_auth_data);

							if (!strcmp(auth_type, "basic"))
							{
								if (!carg->auth_basic)
								{
									carg->auth_basic = calloc(1, sizeof(void*) * auth_size);
									carg->auth_basic_size = auth_size;
								}

								uint64_t b64sz;
								for (uint64_t i = 0; i < auth_size; ++i)
									if (carg->auth_basic[i] == NULL)
									{
										carg->auth_basic[i] = base64_encode(auth_data, auth_data_len, &b64sz);
									}
										
							}
							else if (!strcmp(auth_type, "bearer"))
							{
								if (!carg->auth_bearer)
								{
									carg->auth_bearer = calloc(1, sizeof(void*) * auth_size);
									carg->auth_bearer_size = auth_size;
								}

								for (uint64_t i = 0; i < auth_size; ++i)
									if (carg->auth_bearer[i] == NULL)
										carg->auth_bearer[i] = strndup(auth_data, auth_data_len);
							}
							else if (!strcmp(auth_type, "other"))
							{
								if (!carg->auth_other)
								{
									carg->auth_other = calloc(1, sizeof(void*) * auth_size);
									carg->auth_other_size = auth_size;
								}

								for (uint64_t i = 0; i < auth_size; ++i)
									if (carg->auth_other[i] == NULL)
										carg->auth_other[i] = strndup(auth_data, auth_data_len);
							}

						}
					}

					json_t *tcp_json = json_object_get(entrypoint, "tcp");
					uint64_t tcp_size = json_array_size(tcp_json);
					for (uint64_t i = 0; i < tcp_size; i++)
					{
						json_t *tcp_object = json_array_get(tcp_json, i);
						char *tcp_string = (char*)json_string_value(tcp_object);
						char *port = strstr(tcp_string, ":");

						// delete
						if (method == HTTP_METHOD_DELETE)
						{
							if (port)
							{
								char *host = strndup(tcp_string, port - tcp_string);
								tcp_server_stop(host, strtoll(port+1, NULL, 10));
								free(host);
							}
							else
								tcp_server_stop("0.0.0.0", strtoll(tcp_string, NULL, 10));

							continue;
						}

						// create
						context_arg *passcarg = carg_copy(carg);
						if (port)
						{
							char *host = strndup(tcp_string, port - tcp_string);
							context_arg *ret = tcp_server_init(ac->loop, host, strtoll(port+1, NULL, 10), 0, passcarg);
							(void)(ret);
							//if (!ret)
							//	carg_free(passcarg);
							free(host);
						}
						else
						{
							context_arg *ret = tcp_server_init(ac->loop, "0.0.0.0", strtoll(tcp_string, NULL, 10), 0, passcarg);
							(void)(ret);
							//if (!ret)
							//	carg_free(passcarg);
						}
					}

					json_t *tls_json = json_object_get(entrypoint, "tls");
					uint64_t tls_size = json_array_size(tls_json);
					for (uint64_t i = 0; i < tls_size; i++)
					{
						json_t *tls_object = json_array_get(tls_json, i);
						char *tls_string = (char*)json_string_value(tls_object);

						char *port = strstr(tls_string, ":");

						// delete
						if (method == HTTP_METHOD_DELETE)
						{
							if (port)
							{
								char *host = strndup(tls_string, port - tls_string);
								tcp_server_stop(host, strtoll(port+1, NULL, 10));
								free(host);
							}
							else
								tcp_server_stop("0.0.0.0", strtoll(tls_string, NULL, 10));

							continue;
						}

						// create
						context_arg *passcarg = carg_copy(carg);
						if (port)
						{
							char *host = strndup(tls_string, port - tls_string);
							tcp_server_init(ac->loop, host, strtoll(port+1, NULL, 10), 1, passcarg);
							free(host);
							//context_arg *ret = tcp_server_init(ac->loop, host, strtoll(port+1, NULL, 10), 1, passcarg);
							//if (!ret)
							//	carg_free(passcarg);
						}
						else
						{
							tcp_server_init(ac->loop, "0.0.0.0", strtoll(tls_string, NULL, 10), 1, passcarg);
							//context_arg *ret = tcp_server_init(ac->loop, "0.0.0.0", strtoll(tls_string, NULL, 10), 1, passcarg);
							//if (!ret)
							//	carg_free(passcarg);
						}
					}

					json_t *udp_json = json_object_get(entrypoint, "udp");
					uint64_t udp_size = json_array_size(udp_json);
					for (uint64_t i = 0; i < udp_size; i++)
					{
						json_t *udp_object = json_array_get(udp_json, i);
						char *udp_string = (char*)json_string_value(udp_object);
						char *port = strstr(udp_string, ":");

						// delete
						if (method == HTTP_METHOD_DELETE)
						{
							if (port)
							{
								char *host = strndup(udp_string, port - udp_string);
								udp_server_stop(host, strtoll(port+1, NULL, 10));
								free(host);
							}
							else
								udp_server_stop("0.0.0.0", strtoll(udp_string, NULL, 10));

							continue;
						}

						// create
						context_arg *passcarg = carg_copy(carg);
						if (port)
						{
							char *host = strndup(udp_string, port - udp_string);
							udp_server_init(ac->loop, host, strtoll(port+1, NULL, 10), 0, passcarg);
							free(host);
						}
						else
							udp_server_init(ac->loop, "0.0.0.0", strtoll(udp_string, NULL, 10), 0, passcarg);
					}

					json_t *unix_json = json_object_get(entrypoint, "unix");
					uint64_t unix_size = json_array_size(unix_json);
					for (uint64_t i = 0; i < unix_size; i++)
					{
						json_t *unix_object = json_array_get(unix_json, i);
						char *unix_string = (char*)json_string_value(unix_object);
						if (method == HTTP_METHOD_DELETE)
						{
							unix_server_stop(unix_string);
						}
						else
						{
							context_arg *passcarg = carg_copy(carg);
							unix_server_init(ac->loop, strdup(unix_string), passcarg);
						}
					}

					json_t *unixgram_json = json_object_get(entrypoint, "unixgram");
					uint64_t unixgram_size = json_array_size(unixgram_json);
					for (uint64_t i = 0; i < unixgram_size; i++)
					{
						json_t *unixgram_object = json_array_get(unixgram_json, i);
						char *unixgram_string = (char*)json_string_value(unixgram_object);
						if (method == HTTP_METHOD_DELETE)
						{
							unixgram_server_stop(unixgram_string);
						}
						else
						{
							context_arg *passcarg = carg_copy(carg);
							unixgram_server_init(ac->loop, strdup(unixgram_string), passcarg);
						}
					}
					int8_t ret = 1;
					code = http_error_handler_v1(ret, "Created", "Cannot bind", "", "", 0, status, respbody);
					carg_free(carg);
				}
			}
			if (!strcmp(key, "system"))
			{
				int type = json_typeof(value);
				if (type != JSON_OBJECT)
				{
					code = 400;
					snprintf(status, 100, "Bad Request");
					snprintf(respbody, 1000, "{\"error\": \"tag system is not an object\"}\n");
					if (ac->log_level > 0)
						fprintf(stderr, "%s", respbody);
				}
				else
				{
					const char *system_key;
					json_t *sys_value;
					json_object_foreach(value, system_key, sys_value)
					{
						uint8_t enkey = 1;
						if (method == HTTP_METHOD_DELETE)
							enkey = 0;
						
						if (!strcmp(system_key, "base"))
							ac->system_base = enkey;
						else if (!strcmp(system_key, "disk"))
							ac->system_disk = enkey;
						else if (!strcmp(system_key, "network"))
							ac->system_network = enkey;
						else if (!strcmp(system_key, "smart"))
							ac->system_smart = enkey;
						else if (!strcmp(system_key, "interrupts"))
							ac->system_interrupts = enkey;
						else if (!strcmp(system_key, "firewall"))
						{
							ac->system_firewall = enkey;
							json_t *cvalue = json_object_get(sys_value, "ipset");
							if (cvalue)
							{
								const char *dt = json_string_value(cvalue);
								if (!strcmp(dt, "on"))
									ac->system_ipset = 1;
								else if (!strcmp(dt, "off"))
								{
									ac->system_ipset = 0;
									ac->system_ipset_entries = 0;
								}
								else if (!strcmp(dt, "entries"))
								{
									ac->system_ipset = 1;
									ac->system_ipset_entries = 1;
								}
							}
						}
						else if (!strcmp(system_key, "sysfs"))
						{
							char *sysfs = (char*)json_string_value(sys_value);
							if (sysfs)
							{
								free(ac->system_sysfs);
								ac->system_sysfs = strdup(sysfs);
							}
						}
						else if (!strcmp(system_key, "procfs"))
						{
							char *procfs = (char*)json_string_value(sys_value);
							if (procfs)
							{
								free(ac->system_procfs);
								ac->system_procfs = strdup(procfs);
							}
						}
						else if (!strcmp(system_key, "rundir"))
						{
							char *rundir = (char*)json_string_value(sys_value);
							if (rundir)
							{
								free(ac->system_rundir);
								ac->system_rundir = strdup(rundir);
							}
						}
						else if (!strcmp(system_key, "usrdir"))
						{
							char *usrdir = (char*)json_string_value(sys_value);
							if (usrdir)
							{
								free(ac->system_usrdir);
								ac->system_usrdir = strdup(usrdir);
							}
						}
						else if (!strcmp(system_key, "etcdir"))
						{
							char *etcdir = (char*)json_string_value(sys_value);
							if (etcdir)
							{
								free(ac->system_etcdir);
								ac->system_etcdir = strdup(etcdir);
							}
						}
						else if (!strcmp(system_key, "cadvisor"))
						{
							ac->system_cadvisor = enkey;
							json_t *cvalue = json_object_get(sys_value, "docker");
							char *dockersock = cvalue ? (char*)json_string_value(cvalue) : DOCKERSOCK;
							mkdirp("/var/lib/alligator/nsmount");

							host_aggregator_info *hi = parse_url(dockersock, strlen(dockersock));

							char *query = gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 0, "1.0", NULL, NULL, NULL);
							context_arg *carg = context_arg_json_fill(cvalue, hi, docker_labels, "docker_labels", query, 0, NULL, NULL, 0, ac->loop, NULL, 0, NULL, 0);
							if (!smart_aggregator(carg))
								carg_free(carg);

							url_free(hi);
						}
						else if (!strcmp(system_key, "cpuavg"))
						{
							ac->system_cpuavg = enkey;
							ac->system_cpuavg_period = 5*60;
							json_t *cvalue = json_object_get(sys_value, "period");
							if (cvalue)
							{
								ac->system_cpuavg_period = (json_integer_value(cvalue)*60);
							}

							if (ac->system_avg_metrics)
								free(ac->system_avg_metrics);

							ac->system_avg_metrics = calloc(1, sizeof(double)*ac->system_cpuavg_period);
						}
						else if (!strcmp(system_key, "packages"))
						{
							ac->system_packages = enkey;
							uint64_t packages_size = json_array_size(sys_value);

							// disable scrape if packages not selected
							if (enkey || (!enkey && !packages_size))
								ac->system_packages = enkey;

							// enable or disable selected packageses
							for (uint64_t i = 0; i < packages_size; i++)
							{
								json_t *packages_obj = json_array_get(sys_value, i);
								int obj_type = json_typeof(packages_obj);
								if (obj_type == JSON_STRING)
								{
									char *obj_str = (char*)json_string_value(packages_obj);
									uint64_t obj_len = json_string_length(packages_obj);
									if (enkey)
										match_push(ac->packages_match, obj_str, obj_len);
									else
										match_del(ac->packages_match, obj_str, obj_len);
								}
							}
						}
						else if (!strcmp(system_key, "services"))
						{
							ac->system_services = enkey;
							uint64_t services_size = json_array_size(sys_value);

							// disable scrape if services not selected
							if (enkey || (!enkey && !services_size))
								ac->system_services = enkey;

							// enable or disable selected serviceses
							for (uint64_t i = 0; i < services_size; i++)
							{
								json_t *services_obj = json_array_get(sys_value, i);
								int obj_type = json_typeof(services_obj);
								if (obj_type == JSON_STRING)
								{
									char *obj_str = (char*)json_string_value(services_obj);
									uint64_t obj_len = json_string_length(services_obj);
									if (enkey)
										match_push(ac->services_match, obj_str, obj_len);
									else
										match_del(ac->services_match, obj_str, obj_len);
								}
							}
						}
						else if (!strcmp(system_key, "process"))
						{
							uint64_t process_size = json_array_size(sys_value);

							// disable scrape if processes not selected
							if (enkey || (!enkey && !process_size))
								ac->system_process = enkey;

							// enable or disable selected processes
							for (uint64_t i = 0; i < process_size; i++)
							{
								json_t *process_obj = json_array_get(sys_value, i);
								int obj_type = json_typeof(process_obj);
								if (obj_type == JSON_STRING)
								{
									char *obj_str = (char*)json_string_value(process_obj);
									uint64_t obj_len = json_string_length(process_obj);
									if (enkey)
										match_push(ac->process_match, obj_str, obj_len);
									else
										match_del(ac->process_match, obj_str, obj_len);
								}
							}
						}
						else if (!strcmp(system_key, "pidfile"))
						{
							uint64_t pidfile_size = json_array_size(sys_value);

							// enable or disable selected processes
							for (uint64_t i = 0; i < pidfile_size; i++)
							{
								json_t *pidfile_obj = json_array_get(sys_value, i);
								int obj_type = json_typeof(pidfile_obj);
								if (obj_type == JSON_STRING)
								{
									char *obj_str = (char*)json_string_value(pidfile_obj);
									//uint64_t obj_len = json_string_length(pidfile_obj);
									if (enkey)
										pidfile_push(obj_str, 0);
									else
										pidfile_del(obj_str, 0);
								}
							}
						}
						else if (!strcmp(system_key, "cgroup"))
						{
							uint64_t pidfile_size = json_array_size(sys_value);

							// enable or disable selected processes
							for (uint64_t i = 0; i < pidfile_size; i++)
							{
								json_t *pidfile_obj = json_array_get(sys_value, i);
								int obj_type = json_typeof(pidfile_obj);
								if (obj_type == JSON_STRING)
								{
									char *obj_str = (char*)json_string_value(pidfile_obj);
									//uint64_t obj_len = json_string_length(pidfile_obj);
									if (enkey)
										pidfile_push(obj_str, 1);
									else
										pidfile_del(obj_str, 1);
								}
							}
						}
						else if (!strcmp(system_key, "userprocess"))
						{
							uint64_t userprocess_size = json_array_size(sys_value);

							// enable or disable selected processes
							for (uint64_t i = 0; i < userprocess_size; i++)
							{
								json_t *userprocess_obj = json_array_get(sys_value, i);
								int obj_type = json_typeof(userprocess_obj);
								if (obj_type == JSON_STRING)
								{
									char *obj_str = (char*)json_string_value(userprocess_obj);
									if (enkey)
										userprocess_push(ac->system_userprocess, obj_str);
									else
										userprocess_del(ac->system_userprocess, obj_str);
								}
							}
						}
						else if (!strcmp(system_key, "groupprocess"))
						{
							uint64_t groupprocess_size = json_array_size(sys_value);

							// enable or disable selected processes
							for (uint64_t i = 0; i < groupprocess_size; i++)
							{
								json_t *groupprocess_obj = json_array_get(sys_value, i);
								int obj_type = json_typeof(groupprocess_obj);
								if (obj_type == JSON_STRING)
								{
									char *obj_str = (char*)json_string_value(groupprocess_obj);
									if (enkey)
										userprocess_push(ac->system_groupprocess, obj_str);
									else
										userprocess_del(ac->system_groupprocess, obj_str);
								}
							}
						}
						else if (!strcmp(system_key, "sysctl"))
						{
							uint64_t sysctl_size = json_array_size(sys_value);

							// enable or disable selected processes
							for (uint64_t i = 0; i < sysctl_size; i++)
							{
								json_t *sysctl_obj = json_array_get(sys_value, i);
								int obj_type = json_typeof(sysctl_obj);
								if (obj_type == JSON_STRING)
								{
									char *obj_str = (char*)json_string_value(sysctl_obj);
									if (enkey)
										sysctl_push(ac->system_sysctl, obj_str);
									else
										sysctl_del(ac->system_sysctl, obj_str);
								}
							}
						}
						else if (!strcmp(system_key, "log_level"))
						{
							if (json_typeof(sys_value) == JSON_STRING)
								ac->system_carg->log_level = get_log_level_by_name(json_string_value(sys_value), json_string_length(sys_value));
							else
								ac->system_carg->log_level = json_integer_value(sys_value);
						}
					}
					code = 202;
					snprintf(status, 100, "Accepted");
					snprintf(respbody, 1000, "{\"success\": \"accepted\"}\n");
					if (ac->log_level > 0)
						fprintf(stderr, "%s", respbody);
				}
			}
			if (!strcmp(key, "aggregate"))
			{
				uint8_t enkey = 1;
				if (method == HTTP_METHOD_DELETE)
					enkey = 0;

				uint64_t aggregate_size = json_array_size(value);
				for (uint64_t i = 0; i < aggregate_size; i++)
				{
					json_t *aggregate = json_array_get(value, i);

					json_t *jhandler = json_object_get(aggregate, "handler");
					if (!jhandler)
						continue;

					char *handler = (char*)json_string_value(jhandler);

					json_t *jurl = json_object_get(aggregate, "url");
					if (!jurl)
						continue;

					char *url = (char*)json_string_value(jurl);

					uint8_t follow_redirects = config_get_intstr_json(aggregate, "follow_redirects");

					host_aggregator_info *hi = parse_url(url, strlen(url));
					//char *query;
					//if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
					//	query = gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 0, NULL);
					//else
					//	query = hi->query;

					aggregate_context *actx = alligator_ht_search(ac->aggregate_ctx, actx_compare, handler, tommy_strhash_u32(0, handler));
					if (!actx)
						continue;
					alligator_ht *env = env_struct_parser(aggregate);

					// TODO: potential memory leak
					if (actx->data_func)
						actx->data = actx->data_func(hi, actx, aggregate);

					for (uint64_t j = 0; j < actx->handlers; j++)
					{
						string *query = NULL;
						char *writemesg = NULL;
						uint64_t writelen = 0;
						if (actx->handler[j].mesg_func)
						{
							// check for NULL (non-write aggregates)
							query = actx->handler[j].mesg_func(hi, actx, env, NULL);
							if (query && query != (string*)-1)
							{
								writemesg = query->s;
								writelen = query->l;
								free(query);
							}
						}

						if (enkey)
						{
							context_arg *carg = context_arg_json_fill(aggregate, hi, actx->handler[j].name, actx->handler[j].key, writemesg, writelen, actx->data, actx->handler[j].validator, actx->handler[j].headers_pass, ac->loop, env, follow_redirects, NULL, 0);
							if (!smart_aggregator(carg))
							{
								carg_free(carg);
								snprintf(status, 100, "ERROR");
								snprintf(respbody, 1000, "Error (maybe already created?)");
							}
							else
							{
								snprintf(status, 100, "OK");
								snprintf(respbody, 1000, "Accepted");
							}
						}
						else
						{
							smart_aggregator_del_key_gen(hi->transport_string, actx->handler[j].key, hi->host, hi->port, hi->query);
							snprintf(status, 100, "OK");
							snprintf(respbody, 1000, "Deleted OK");
						}
					}

					//free env
					env_free(env);

					//free hi
					url_free(hi);
				}
			}
			if (!strcmp(key, "puppeteer"))
			{
				if (method == HTTP_METHOD_DELETE)
					puppeteer_delete(value);
				else
					puppeteer_insert(value);
			}
		}

		json_decref(root);
	}

	if (response)
	{
		snprintf(temp_resp, 1200, "HTTP/1.1 %"u16" %s\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n%s\n", code, status, respbody);
		string_cat(response, temp_resp, strlen(temp_resp));
	}
}
