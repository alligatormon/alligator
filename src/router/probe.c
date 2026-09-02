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
#include "common/logs.h"
#include "events/proxy.h"

#define HTTP_PROBE_HANDLER "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"
#define HTTP_PROBE_HANDLER_ERR "HTTP/1.1 400 Bad Request\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"

void probe_router(string *response, http_reply_data* http_data, context_arg *carg)
{
	alligator_ht *args = http_get_args(http_data->uri, http_data->uri_size);

	http_arg *harg = alligator_ht_search(args, http_arg_compare, "module", tommy_strhash_u32(0, "module"));
	if (!harg)
	{
		carglog(carg, L_WARN, "no arg 'module' in query '%s'\n", http_data->uri);

		string_cat(response, "no arg 'module' in query '", 26);
		string_cat(response, http_data->uri, http_data->uri_size);
		string_cat(response, "'", 1);

		http_args_free(args);
		return;
	}

	char *module = harg->value;

	harg = alligator_ht_search(args, http_arg_compare, "target", tommy_strhash_u32(0, "target"));
	if (!harg)
	{
		carglog(carg, L_WARN, "no arg 'target' in query '%s'\n", http_data->uri);

		string_cat(response, "no arg 'target' in query '", 26);
		string_cat(response, http_data->uri, http_data->uri_size);
		string_cat(response, "'", 1);

		http_args_free(args);
		return;
	}

	char *target = harg->value;

	char url[1024];
	uint64_t url_size;
	probe_node* pn = probe_get(module);
	if (!pn)
	{
		carglog(carg, L_WARN, "no such module '%s' in query '%s'\n", module, http_data->uri);
		string_cat(response, HTTP_PROBE_HANDLER_ERR, strlen(HTTP_PROBE_HANDLER_ERR));
		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		string_cat(response, "\r\n", 2);
		string_cat(response, "no such module '", 16);
		string_cat(response, module, strlen(module));
		string_cat(response, "' in query'", 11);
		string_cat(response, http_data->uri, http_data->uri_size);
		string_cat(response, "'", 1);

		http_args_free(args);
		return;
	}

	url_size = snprintf(url, sizeof(url), "%s%s", pn->scheme, target);
	if (url_size >= sizeof(url))
		url_size = sizeof(url) - 1;

	host_aggregator_info *hi = parse_url(url, url_size);
	context_arg *new_carg;
	if ((pn->prober == APROTO_HTTP) || (pn->prober == APROTO_HTTPS))
	{
		char *http_query = gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, NULL, pn->env, NULL, NULL);
		new_carg = context_arg_json_fill(NULL, hi, blackbox_null, "blackbox_null", http_query, 0, pn, NULL, 0, carg->loop, pn->env, pn->follow_redirects, NULL, 0);
	}
	else
	{
		new_carg = context_arg_json_fill(NULL, hi, blackbox_null, "blackbox_null", hi->query, 0, pn, NULL, 0, carg->loop, pn->env, pn->follow_redirects, NULL, 0);
	}

	if (pn->http_proxy_url) {
		new_carg->proxy = proxy_parse_url(pn->http_proxy_url);
		if (!new_carg->proxy)
			carglog(carg, L_ERROR, "probe: cannot parse proxy '%s'\n", pn->http_proxy_url);
		else if (!proxy_ok_for_transport(new_carg->proxy, new_carg->transport)) {
			carglog(carg, L_ERROR, "probe: HTTP proxy is not supported for UDP\n");
			proxy_settings_free(new_carg->proxy);
			new_carg->proxy = NULL;
		} else {
			http_request_apply_proxy(new_carg);
		}
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
	string *body = metric_query_deserialize(response->m, mqc, METRIC_SERIALIZER_OPENMETRICS, 0, NULL, NULL, NULL, NULL, NULL);
	query_context_free(mqc);

	char *content_length = malloc(255);
	snprintf(content_length, 255, "Content-Length: %zu\r\n\r\n", body->l);

	string_cat(response, HTTP_PROBE_HANDLER, strlen(HTTP_PROBE_HANDLER));
	if (carg->env)
		alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
	string_cat(response, content_length, strlen(content_length));
	string_cat(response, body->s, body->l);

	free(content_length);
	string_free(body);

	http_args_free(args);
}
