#include <jansson.h>
#include "common/selector.h"
#include "events/context_arg.h"
#include "parsers/http_proto.h"
#include "metric/namespace.h"
#include "common/http.h"
#include "query/promql.h"
#include "metric/query.h"

void dsv_router(string *response, http_reply_data* http_data, context_arg *carg)
{
	alligator_ht *args = http_get_args(http_data->uri, http_data->uri_size);
	char *query = "{}";
	char delimiter = '|';

	http_arg *harg = alligator_ht_search(args, http_arg_compare, "query", tommy_strhash_u32(0, "query"));
	if (harg)
		query = harg->value;

	harg = alligator_ht_search(args, http_arg_compare, "delimiter", tommy_strhash_u32(0, "delimiter"));
	if (harg)
		delimiter = *harg->value;

	metric_query_context *mqc = promql_parser(NULL, query, strlen(query));

	query_context_convert_http_args_to_query(mqc, args);

	string *body = metric_query_deserialize(response->m, mqc, METRIC_SERIALIZER_DSV, delimiter, NULL, NULL, NULL, NULL, NULL);

	char *content_length = malloc(255);
	snprintf(content_length, 255, "Content-Length: %zu\r\n\r\n", body->l);

	string_cat(response, "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n", strlen("HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"));
	if (carg->env)
		alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
	string_cat(response, content_length, strlen(content_length));
	string_cat(response, body->s, body->l);

	free(content_length);
	string_free(body);

	http_args_free(args);
	query_context_free(mqc);
}
