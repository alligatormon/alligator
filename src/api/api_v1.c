#include <jansson.h>
#include "main.h"
#include "parsers/http_proto.h"
#include "config/mapping.h"
#include "common/url.h"
#include "common/http.h"
#include "cadvisor/run.h"
#include "events/context_arg.h"
#include "lang/lang.h"
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

void http_api_v1(string *response, http_reply_data* http_data, char *configbody)
{
	extern aconf *ac;
	char *body = http_data ? http_data->body : configbody;
	uint8_t method = http_data ? http_data->method : HTTP_METHOD_PUT;
	uint16_t code = 200;
	char temp_resp[1000];
	char respbody[1000];
	char status[100];
	snprintf(status, 100, "OK");

	json_error_t error;
	json_t *root = json_loads(body, 0, &error);
	if (!root)
	{
		code = 400;
		snprintf(status, 100, "Bad Request");
		snprintf(respbody, 1000, "json error on line %d: %s\n", error.line, error.text);
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
				ac->log_level = json_integer_value(value);
			}
			if (!strcmp(key, "ttl"))
			{
				ac->ttl = json_integer_value(value);
			}
			if (!strcmp(key, "modules"))
			{
				uint64_t modules_size = json_array_size(value);
				for (uint64_t i = 0; i < modules_size; i++)
				{
					json_t *modules = json_array_get(value, i);
					json_t *jkey = json_object_get(modules, "key");
					if (!jkey)
						continue;
					char *key = (char*)json_string_value(jkey);

					json_t *jpath = json_object_get(modules, "path");
					if (!jpath)
						continue;
					char *path = (char*)json_string_value(jpath);

					if (method == HTTP_METHOD_DELETE)
					{
						module_t *module = tommy_hashdyn_search(ac->modules, module_compare, key, tommy_strhash_u32(0, key));
						free(module->key);
						free(module->path);
						tommy_hashdyn_remove_existing(ac->modules, &(module->node));

						free(module);
						continue;
					}

					module_t *module = calloc(1, sizeof(*module));
					module->key = strdup(key);
					module->path = strdup(path);

					tommy_hashdyn_insert(ac->modules, &(module->node), module, tommy_strhash_u32(0, module->key));
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

					if (method == HTTP_METHOD_DELETE)
					{
						tls_fs_del(name);
						continue;
					}

					tls_fs_push(name, path, match);
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
					ac->ttl = json_integer_value(period)*1000;
				else
					ac->ttl = 15000;
					
			}
			if (!strcmp(key, "lang"))
			{
				uint64_t lang_size = json_array_size(value);
				for (uint64_t i = 0; i < lang_size; i++)
				{
					json_t *lang = json_array_get(value, i);

					json_t *lang_key = json_object_get(lang, "key");
					if (!lang_key)
						continue;
					char *key = (char*)json_string_value(lang_key);

					if (method == HTTP_METHOD_DELETE)
					{
						lang_delete(key);
					}

					json_t *lang_name = json_object_get(lang, "lang");
					if (!lang_name)
						continue;
					char *slang_name = (char*)json_string_value(lang_name);

					json_t *lang_classpath = json_object_get(lang, "classpath");
					char *slang_classpath = (char*)json_string_value(lang_classpath);

					json_t *lang_classname = json_object_get(lang, "classname");
					char *slang_classname = (char*)json_string_value(lang_classname);

					json_t *lang_method = json_object_get(lang, "method");
					char *slang_method = (char*)json_string_value(lang_method);

					json_t *lang_arg = json_object_get(lang, "arg");
					char *slang_arg = (char*)json_string_value(lang_arg);

					lang_options *lo = calloc(1, sizeof(*lo));
					lo->lang = strdup(slang_name);
					lo->key = strdup(key);

					if (slang_classpath)
						lo->classpath = strdup(slang_classpath);
					if (slang_classname)
						lo->classname = strdup(slang_classname);
					if (slang_method)
						lo->method = strdup(slang_method);
					if (slang_arg)
						lo->arg = strdup(slang_arg);

					lang_push(lo);
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

				context_arg *carg = calloc(1, sizeof(*carg));
				carg->buffer_request_size = 6553500;
				carg->buffer_response_size = 6553500;
				carg->net_acl = calloc(1, sizeof(*carg->net_acl));

				json_t *carg_ttl = json_object_get(value, "ttl");
				carg->ttl = json_integer_value(carg_ttl);

				json_t *carg_handler = json_object_get(value, "handler");
				char *str_handler = (char*)json_string_value(carg_handler);
				if (str_handler && !strcmp(str_handler, "rsyslog-impstats"))
					carg->parser_handler =  &rsyslog_impstats_handler;
				else if (str_handler && !strcmp(str_handler, "log"))
					carg->parser_handler =  &log_handler;
				
				json_t *allow = json_object_get(value, "allow");
				if (allow)
				{
					uint64_t allow_size = json_array_size(allow);
					for (uint64_t i = 0; i < allow_size; i++)
					{
						json_t *allow_obj = json_array_get(allow, i);
						char *allow_str = (char*)json_string_value(allow_obj);
						
						if (method == HTTP_METHOD_DELETE)
							network_range_delete(carg->net_acl, allow_str);
						else
							network_range_push(carg->net_acl, allow_str, IPACCESS_ALLOW);
					}
				}
				json_t *deny = json_object_get(value, "deny");
				if (deny)
				{
					uint64_t deny_size = json_array_size(deny);
					for (uint64_t i = 0; i < deny_size; i++)
					{
						json_t *deny_obj = json_array_get(deny, i);
						char *deny_str = (char*)json_string_value(deny_obj);
						
						if (method == HTTP_METHOD_DELETE)
							network_range_delete(carg->net_acl, deny_str);
						else
							network_range_push(carg->net_acl, deny_str, IPACCESS_DENY);
					}
				}

				json_t *reject = json_object_get(value, "reject");
				if (reject)
				{
					if (!carg->reject)
					{
						carg->reject = calloc(1, sizeof(*carg->reject));
						tommy_hashdyn_init(carg->reject);
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
				json_t *mapping = json_object_get(value, "mapping");
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

				json_t *tcp_json = json_object_get(value, "tcp");
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
						tcp_server_init(ac->loop, host, strtoll(port+1, NULL, 10), 0, passcarg);
					}
					else
						tcp_server_init(ac->loop, "0.0.0.0", strtoll(tcp_string, NULL, 10), 0, passcarg);
				}

				json_t *tls_json = json_object_get(value, "tls");
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
					}
					else
						tcp_server_init(ac->loop, "0.0.0.0", strtoll(tls_string, NULL, 10), 1, passcarg);
				}

				json_t *udp_json = json_object_get(value, "udp");
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
					}
					else
						udp_server_init(ac->loop, "0.0.0.0", strtoll(udp_string, NULL, 10), 0, passcarg);
				}

				json_t *unix_json = json_object_get(value, "unix");
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

				json_t *unixgram_json = json_object_get(value, "unixgram");
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
				carg_free(carg);
				int8_t ret = 1;
				code = http_error_handler_v1(ret, "Created", "Cannot bind", "", "", 0, status, respbody);
			}
			if (!strcmp(key, "system"))
			{
				int type = json_typeof(value);
				if (type != JSON_OBJECT)
				{
					code = 400;
					snprintf(status, 100, "Bad Request");
					snprintf(respbody, 1000, "{\"error\": \"tag system is not an object\"}");
					fprintf(stderr, "%s", respbody);
				}
				else
				{
					const char *system_key;
					json_t *sys_value;
					json_object_foreach(value, system_key, sys_value)
					{
						printf("key: %s\n", system_key);
						uint8_t enkey = 1;
						if (method == HTTP_METHOD_DELETE)
							enkey = 0;
						
						if (!strcmp(system_key, "base"))
							ac->system_base = enkey;
						else if (!strcmp(system_key, "disk"))
							ac->system_disk = enkey;
						else if (!strcmp(system_key, "network"))
							ac->system_network = enkey;
						else if (!strcmp(system_key, "vm"))
							ac->system_vm = enkey;
						else if (!strcmp(system_key, "smart"))
							ac->system_smart = enkey;
						else if (!strcmp(system_key, "firewall"))
							ac->system_firewall = enkey;
						else if (!strcmp(system_key, "cadvisor"))
						{
							ac->system_cadvisor = enkey;
							json_t *cvalue = json_object_get(sys_value, "docker");
							char *dockersock = cvalue ? (char*)json_string_value(cvalue) : DOCKERSOCK;

							host_aggregator_info *hi = parse_url(dockersock, strlen(dockersock));
							char *query = gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 0);
							context_arg *carg = context_arg_json_fill(cvalue, hi, docker_labels, "docker_labels", query, 0, NULL, NULL, ac->loop);
							smart_aggregator(carg);
						}
						else if (!strcmp(system_key, "packages"))
						{
							if (!ac->rpmlib)
								ac->rpmlib = rpm_library_init();

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
					}
					code = 202;
					snprintf(status, 100, "Accepted");
					snprintf(respbody, 1000, "{\"success\": \"accepted\"}");
					fprintf(stderr, "%s", respbody);
				}
			}
			if (!strcmp(key, "aggregate"))
			{
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

					host_aggregator_info *hi = parse_url(url, strlen(url));
					//char *query;
					//if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
					//	query = gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 0);
					//else
					//	query = hi->query;

					aggregate_context *actx = tommy_hashdyn_search(ac->aggregate_ctx, actx_compare, handler, tommy_strhash_u32(0, handler));
					if (!actx)
						continue;

					for (uint64_t j = 0; j < actx->handlers; j++)
					{
						string *query = actx->handler[j].mesg_func(hi, NULL);
						context_arg *carg = context_arg_json_fill(aggregate, hi, actx->handler[j].name, actx->handler[j].key, query->s, query->l, actx->data, actx->handler[j].validator, ac->loop);
						smart_aggregator(carg);
					}
				}
			}



			if (!strcmp(key, "configuration"))
			{
				uint64_t configuration_size = json_array_size(value);
				for (uint64_t i = 0; i < configuration_size; i++)
				{
					json_t *configuration = json_array_get(value, i);

					json_t *jhandler = json_object_get(configuration, "handler");
					if (!jhandler)
						continue;

					char *handler = (char*)json_string_value(jhandler);
					size_t handler_len = json_string_length(jhandler)+3;
					char *shandler = malloc(handler_len);
					snprintf(shandler, handler_len, "%s-sd", handler);

					json_t *jurl = json_object_get(configuration, "url");
					if (!jurl)
						continue;

					char *url = (char*)json_string_value(jurl);

					host_aggregator_info *hi = parse_url(url, strlen(url));

					aggregate_context *actx = tommy_hashdyn_search(ac->aggregate_ctx, actx_compare, shandler, tommy_strhash_u32(0, shandler));
					if (!actx)
						continue;

					for (uint64_t j = 0; j < actx->handlers; j++)
					{
						string *query = actx->handler[j].mesg_func(hi, NULL);
						context_arg *carg = context_arg_json_fill(configuration, hi, actx->handler[j].name, actx->handler[j].key, query->s, query->l, actx->data, actx->handler[j].validator, ac->loop);
						smart_aggregator(carg);
					}
				}



			}
		}

		json_decref(root);
	}

	if (response)
	{
		snprintf(temp_resp, 1000, "HTTP/1.1 %"u16" %s\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n%s\n", code, status, respbody);
		string_cat(response, temp_resp, strlen(temp_resp));
	}
}
