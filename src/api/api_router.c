#include "main.h"
#include "parsers/http_proto.h"
#define HTTP_STATUS_API_VERSION_NOT_FOUND "HTTP/1.1 404 No found api version\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
#define HTTP_STATUS_API_DISABLED "HTTP/1.1 403 API disabled\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n{\"error\": \"api disabled\"}\n"
void api_router(string *response, http_reply_data* http_data, context_arg *carg)
{
	if (carg && !carg->api_enable)
	{
		if (ac->log_level > 0)
			puts("API request error: api disabled");
		string_cat(response, HTTP_STATUS_API_DISABLED, strlen(HTTP_STATUS_API_DISABLED));
	}
	else if (!strncmp(http_data->uri, "/api/v1/", 8))
		http_api_v1(response, http_data, NULL);
	else
		string_cat(response, HTTP_STATUS_API_VERSION_NOT_FOUND, strlen(HTTP_STATUS_API_VERSION_NOT_FOUND));
}
