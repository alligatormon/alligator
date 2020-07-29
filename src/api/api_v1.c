#include <jansson.h>
#include "main.h"
#include "parsers/http_proto.h"

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

void http_api_v1(string *response, http_reply_data* http_data)
{
	extern aconf *ac;
	char *body = http_data->body;
	puts(body);
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
			printf("key %s\n", key);
			if (!strcmp(key, "log_level"))
			{
				ac->log_level = json_integer_value(value);
			}
			if (!strcmp(key, "entrypoint"))
			{
				void (*handler)(char*, size_t, context_arg*) = 0;

				json_t *proto_json = json_object_get(value, "proto");
				if (!proto_json)
					return;
				const char* proto = json_string_value(proto_json);

				json_t *address_json = json_object_get(value, "address");
				if (!address_json)
					return;
				const char* address = json_string_value(address_json);

				int64_t port = 0;
				printf("port %"d64", address %p, handler %p\n", port, address, handler);
				json_t *port_json = json_object_get(value, "port");
				if (port_json)
					port = json_integer_value(port_json);

				json_t *handler_parser_json = json_object_get(value, "handler");
				if (handler_parser_json)
				{
					const char* handler_parser = json_string_value(handler_parser_json);
					if (!strcmp(handler_parser, "rsyslog-impstats"))
						handler = &rsyslog_impstats_handler;
				}

				context_arg *carg = calloc(1, sizeof(*carg));

				if (!strcmp(proto, "tcp") || !strcmp(proto, "tls"))
				{
					uint8_t tls = !strcmp(proto, "tls") ? 1 : 0;
					if (http_data->method == HTTP_METHOD_PUT || http_data->method == HTTP_METHOD_POST)
					{
						int ret = 0;
						if (tcp_server_init(ac->loop, address, port, tls, carg))
							ret = 1;
						code = http_error_handler_v1(ret, "Created", "Cannot bind", proto, address, port, status, respbody);
					}
					if (http_data->method == HTTP_METHOD_DELETE)
					{
						//int8_t ret = tcp_server_handler_free(address, port);
						//code = http_error_handler_v1(ret, "Deleted", "Not Found", proto, address, port, status, respbody);
					}
				}

				//else if (!strcmp(proto, "udp"))
				//	udp_server_handler(strdup(address), port, handler, carg);

				//else if (!strcmp(proto, "unixgram"))
				//	unixgram_server_handler(strdup(address), handler);

				//else if (!strcmp(proto, "unix"))
				//	unix_server_handler(strdup(address), handler);
			}
			if (!strcmp(key, "system"))
			{
				puts("SYSTEM!!!");
				int type = json_typeof(value);
				printf("type is %d\n", type);

				uint64_t system_index;
				size_t system_size = json_array_size(value);
				for (system_index = 0; system_index < system_size; system_index++)
				{
					json_t *system_obj = json_array_get(value, system_index);
					char *system_key = (char*)json_string_value(system_obj);
					printf("key: %s\n", system_key);
					uint8_t enkey;
					if (http_data->method == HTTP_METHOD_PUT || http_data->method == HTTP_METHOD_POST)
						enkey = 1;
					else if (http_data->method == HTTP_METHOD_DELETE)
						enkey = 0;
					else
						continue;

					if (!strcmp(system_key, "base"))
						ac->system_base = enkey;
					else if (!strcmp(system_key, "disk"))
						ac->system_disk = enkey;
					else if (!strcmp(system_key, "network"))
						ac->system_network = enkey;
					else if (!strcmp(system_key, "process"))
						ac->system_process = enkey;
					else if (!strcmp(system_key, "vm"))
						ac->system_vm = enkey;
					else if (!strcmp(system_key, "smart"))
						ac->system_smart = enkey;
					else if (!strcmp(system_key, "packages"))
						ac->system_packages = enkey;
				}
			}
		}

		json_decref(root);
	}

	snprintf(temp_resp, 1000, "HTTP/1.1 %"u16" %s\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n%s\n", code, status, respbody);
	string_cat(response, temp_resp, strlen(temp_resp));
}
