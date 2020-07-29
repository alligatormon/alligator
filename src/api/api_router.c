#include "main.h"
#include "parsers/http_proto.h"
#define HTTP_STATUS_API_VERSION_NOT_FOUND "HTTP/1.1 404 No found api version\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
void api_router(string *response, http_reply_data* http_data)
{
		if (!strncmp(http_data->uri, "/api/v1/", 8))
			http_api_v1(response, http_data);
		else
			string_cat(response, HTTP_STATUS_API_VERSION_NOT_FOUND, strlen(HTTP_STATUS_API_VERSION_NOT_FOUND));
}
