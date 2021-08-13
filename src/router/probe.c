#include "parsers/http_proto.h"
#include "common/selector.h"
#include "events/context_arg.h"
#include <string.h>
#include <common/http.h>
#include "parsers/multiparser.h"
#include "metric/namespace.h"
#include "metric/query.h"
#include "query/promql.h"
#include "probe/probe.h"

#define HTTP_PROBE_HANDLER "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
#define HTTP_PROBE_HANDLER_ERR "HTTP/1.1 400 Bad Request\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"

void probe_router(string *response, http_reply_data* http_data, context_arg *carg)
{
	alligator_ht *args = http_get_args(http_data->uri, http_data->uri_size);

	http_arg *harg = alligator_ht_search(args, http_arg_compare, "module", tommy_strhash_u32(0, "module"));
	if (!harg)
	{
		if (carg->log_level > 0)
			printf("no arg 'module' in query '%s'\n", http_data->uri);

		string_cat(response, "no arg 'module' in query '", 26);
		string_cat(response, http_data->uri, http_data->uri_size);
		string_cat(response, "'", 1);

		return;
	}

	char *module = harg->value;

	harg = alligator_ht_search(args, http_arg_compare, "target", tommy_strhash_u32(0, "target"));
	if (!harg)
	{
		if (carg->log_level > 0)
			printf("no arg 'target' in query '%s'\n", http_data->uri);

		string_cat(response, "no arg 'target' in query '", 26);
		string_cat(response, http_data->uri, http_data->uri_size);
		string_cat(response, "'", 1);

		return;
	}

	char *target = harg->value;

	char url[1024];
	uint64_t url_size;
	probe_node* pn = probe_get(module);
	if (!pn)
	{
		if (carg->log_level > 0)
			printf("no such module '%s' in query '%s'\n", module, http_data->uri);
		string_cat(response, HTTP_PROBE_HANDLER_ERR, strlen(HTTP_PROBE_HANDLER_ERR));
		string_cat(response, "no such module '", 16);
		string_cat(response, module, strlen(module));
		string_cat(response, "' in query'", 11);
		string_cat(response, http_data->uri, http_data->uri_size);
		string_cat(response, "'", 1);

		return;
	}

	url_size = strlcpy(url, pn->scheme, 1024);
	url_size += strlcpy(url + url_size, target, 1024 - url_size);

	host_aggregator_info *hi = parse_url(url, url_size);
	alligator_ht *env = env_struct_duplicate(pn->env);
	context_arg *new_carg;
	if ((pn->prober == APROTO_HTTP) || (pn->prober == APROTO_HTTPS))
	{
		char *http_query = gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, 0, NULL, env, pn->http_proxy_url, NULL);
		new_carg = context_arg_json_fill(NULL, hi, blackbox_null, "blackbox_null", http_query, 0, pn, NULL, 0, carg->loop, env, pn->follow_redirects, NULL, 0);
	}
	else
	{
		new_carg = context_arg_json_fill(NULL, hi, blackbox_null, "blackbox_null", hi->query, 0, pn, NULL, 0, carg->loop, env, pn->follow_redirects, NULL, 0);
	}

	if (pn->ca_file)
		new_carg->tls_ca_file = strdup(pn->ca_file);

	if (pn->cert_file)
		new_carg->tls_cert_file = strdup(pn->cert_file);

	if (pn->key_file)
		new_carg->tls_key_file = strdup(pn->key_file);

	if (pn->server_name)
		new_carg->tls_server_name = strdup(pn->server_name);

	if (pn->labels)
	{
		alligator_ht *labels = labels_dup(pn->labels);
		new_carg->labels = labels;
	}

	if (pn->loop)
		new_carg->pingloop = pn->loop;

	if (pn->loop)
		new_carg->pingpercent_success = pn->percent_success;

	new_carg->tls_verify = pn->tls_verify;

	if (!smart_aggregator(new_carg))
		carg_free(new_carg);

	metric_query_context *mqc = query_context_new(NULL);
	query_context_set_label(mqc, "host", target);
	string *body = metric_query_deserialize(response->m, mqc, METRIC_SERIALIZER_OPENMETRICS, 0, NULL);
	query_context_free(mqc);

	char *content_length = malloc(255);
	snprintf(content_length, 255, "Content-Length: %zu\r\n\r\n", body->l);

	string_cat(response, "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n", strlen("HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"));
	string_cat(response, content_length, strlen(content_length));
	string_cat(response, body->s, body->l);

	free(content_length);
	string_free(body);

	http_args_free(args);
}
