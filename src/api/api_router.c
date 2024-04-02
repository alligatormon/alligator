#include "main.h"
#include "parsers/http_proto.h"
#include "api/api.h"
#define HTTP_STATUS_API_OK "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"
#define HTTP_STATUS_API_VERSION_NOT_FOUND "HTTP/1.1 404 No found api version\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"
#define HTTP_STATUS_API_DISABLED "HTTP/1.1 403 API disabled\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"
#define API_DISABLED_MESG "\r\n{\"error\": \"api disabled\"}\n"
void api_router(string *response, http_reply_data* http_data, context_arg *carg)
{
	if (carg && !carg->api_enable)
	{
		if (ac->log_level > 0)
			puts("API request error: api disabled");
		string_cat(response, HTTP_STATUS_API_DISABLED, strlen(HTTP_STATUS_API_DISABLED));
		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		string_cat(response, API_DISABLED_MESG, strlen(API_DISABLED_MESG));
	}
	else if (!strncmp(http_data->uri, "/api/v1/", 8))
		http_api_v1(response, http_data, NULL);
	else
	{
		string_cat(response, HTTP_STATUS_API_VERSION_NOT_FOUND, strlen(HTTP_STATUS_API_VERSION_NOT_FOUND));
		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		string_cat(response, "\r\n", 2);
	}

	if (carg->rreturn == ENTRYPOINT_RETURN_EMPTY)
	{
		string_null(response);
		string_cat(response, HTTP_STATUS_API_OK, strlen(HTTP_STATUS_API_OK));

		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		string_cat(response, "\r\n", 2);
	}
}
