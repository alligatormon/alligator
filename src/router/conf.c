//#include <jansson.h>
#include "common/selector.h"
#include "events/context_arg.h"
#include "parsers/http_proto.h"
//#include "metric/namespace.h"
//#include "common/http.h"
#include "config/get.h"
#define CONF_GET_OK "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\n"
#define CONF_GET_ERR "HTTP/1.1 404 Not Found\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: 3\r\n\r\n{}\n"

void conf_router(string *response, http_reply_data* http_data, context_arg *carg)
{
	//alligator_ht *args = http_get_args(http_data->uri, http_data->uri_size);

	string *body = config_get_string();
	if (!body)
	{
		string_cat(response, CONF_GET_ERR, strlen(CONF_GET_ERR));
		return;
	}


	char *content_length = malloc(255);
	snprintf(content_length, 255, "Content-Length: %zu\r\n\r\n", body->l);

	string_cat(response, CONF_GET_OK, strlen(CONF_GET_OK));
	string_cat(response, content_length, strlen(content_length));
	string_cat(response, body->s, body->l);

	free(content_length);
	string_free(body);

	//http_args_free(args);
}
