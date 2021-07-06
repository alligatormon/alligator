#include "parsers/http_proto.h"
#include "common/selector.h"
#include <string.h>
#define HTTP_STATUS_HANDLER "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"

void stat_router(string *response, http_reply_data* http_data)
{
	string_cat(response, HTTP_STATUS_HANDLER, strlen(HTTP_STATUS_HANDLER));
	metric_build(0, response);
}
