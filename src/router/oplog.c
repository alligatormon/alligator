#include <jansson.h>
#include "common/selector.h"
#include "events/context_arg.h"
#include "parsers/http_proto.h"
#include "metric/namespace.h"
#include "common/http.h"
#include "cluster/type.h"
#include <stdlib.h>
#define OPLOG_RESPONSE_HEADERS "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"

void oplog_api_response(string *response, string *body)
{
	char *content_length = malloc(255);
	uint64_t size = body ? body->l : 0;
	snprintf(content_length, 255, "Content-Length: %zu\r\n\r\n", size);

	string_cat(response, OPLOG_RESPONSE_HEADERS, strlen(OPLOG_RESPONSE_HEADERS));
	string_cat(response, content_length, strlen(content_length));

	if (body)
	{
		string_string_cat(response, body);
		string_free(body);
	}

	free(content_length);
}

void oplog_get_router(string *response, http_reply_data* http_data, context_arg *carg)
{
	alligator_ht *args = http_get_args(http_data->uri, http_data->uri_size);

	char *name = http_get_param(args, "name");
	char *replica = http_get_param(args, "replica");

	string *body = cluster_get_server_data(replica, name);

	oplog_api_response(response, body);

	http_args_free(args);
}


void oplog_post_router(string *response, http_reply_data* http_data, context_arg *carg)
{
	alligator_ht *args = http_get_args(http_data->uri, http_data->uri_size);

	char *name = http_get_param(args, "name");
	char *replica = http_get_param(args, "replica");

	string *body = cluster_shift_server_data(replica, name);

	oplog_api_response(response, body);

	http_args_free(args);

	if (carg->rreturn == ENTRYPOINT_RETURN_EMPTY)
	{
		string_null(response);
		string_cat(response, OPLOG_RESPONSE_HEADERS, strlen(OPLOG_RESPONSE_HEADERS));
		string_cat(response, "\r\n", 2);
	}
}
