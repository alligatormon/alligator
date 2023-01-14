#include "parsers/http_proto.h"
#include "events/context_arg.h"
#include "common/selector.h"
#include <string.h>
#define HTTP_STATUS_HANDLER "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"

void stat_router(string *response, http_reply_data* http_data, context_arg *carg)
{
	string_cat(response, HTTP_STATUS_HANDLER, strlen(HTTP_STATUS_HANDLER));
	if (carg->env)
		alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
	string_cat(response, "\r\n", 2);
	metric_build(0, response);
}
