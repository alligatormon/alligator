#include "parsers/http_proto.h"
#include "common/selector.h"
#include "events/context_arg.h"
#include <stdio.h>
#include <string.h>
#include <common/http.h>
#include "parsers/multiparser.h"
#include "metric/namespace.h"
#include "metric/query.h"
#include "metric/labels.h"
#include "query/promql.h"
#include "probe/probe.h"
#include "action/type.h"
#include "common/logs.h"
#include "common/http_entrypoint.h"
#include "common/url.h"
#include "events/proxy.h"
#include "resolver/resolver.h"
#include "common/aggregator.h"
#include <ctype.h>
#include <stdlib.h>

#define HTTP_PROBE_HANDLER "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"
#define HTTP_PROBE_HANDLER_ERR "HTTP/1.1 400 Bad Request\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"

static void probe_reply_error(string *response, context_arg *carg, const char *body, size_t body_len)
{
	string_cat(response, HTTP_PROBE_HANDLER_ERR, strlen(HTTP_PROBE_HANDLER_ERR));
	if (carg && carg->env)
		alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
	http_entrypoint_finish_body(response, body, body_len);
}

static uint64_t probe_scrape_timeout_ms(http_reply_data *http_data, uint64_t module_timeout)
{
	const char *hdr;
	const char *p;
	double sec;
	uint64_t ms;

	if (!http_data || !http_data->headers)
		return module_timeout;
	hdr = http_data->headers;
	while (*hdr) {
		if (!strncasecmp(hdr, "X-Prometheus-Scrape-Timeout-Seconds", 35)) {
			p = hdr + 35;
			while (*p == ' ' || *p == '\t' || *p == ':')
				p++;
			sec = strtod(p, NULL);
			if (sec <= 0)
				return module_timeout;
			ms = (uint64_t)(sec * 1000.0);
			if (ms > 500)
				ms -= 500;
			else if (ms > 1)
				ms -= 1;
			if (module_timeout && ms > module_timeout)
				ms = module_timeout;
			if (!ms)
				ms = 1;
			return ms;
		}
		while (*hdr && *hdr != '\n')
			hdr++;
		if (*hdr == '\n')
			hdr++;
	}
	return module_timeout;
}

static void probe_assign_unique_key(context_arg *carg, const char *module)
{
	char defkey[256];
	char uniq[512];
	const char *base;

	if (!carg || !module)
		return;
	if (carg->key && carg->key[0]) {
		base = carg->key;
	} else {
		smart_aggregator_default_key(defkey, carg->transport_string, carg->parser_name,
			carg->host, carg->port, carg->query_url);
		base = defkey;
	}
	snprintf(uniq, sizeof(uniq), "probe:%s:%s", module, base);
	if (carg->key)
		free(carg->key);
	carg->key = strdup(uniq);
	smart_aggregator_key_normalize(carg->key);
}

void probe_router(string *response, http_reply_data* http_data, context_arg *carg)
{
	alligator_ht *args = http_get_args(http_data->uri, http_data->uri_size);
	char errbuf[1024];
	int n;

	http_arg *harg = alligator_ht_search(args, http_arg_compare, "module", tommy_strhash_u32(0, "module"));
	if (!harg)
	{
		carglog(carg, L_WARN, "no arg 'module' in query '%s'\n", http_data->uri);
		n = snprintf(errbuf, sizeof(errbuf), "no arg 'module' in query '%s'", http_data->uri);
		if (n < 0)
			n = 0;
		if ((size_t)n >= sizeof(errbuf))
			n = (int)sizeof(errbuf) - 1;
		probe_reply_error(response, carg, errbuf, (size_t)n);
		http_args_free(args);
		return;
	}

	char *module = harg->value;

	harg = alligator_ht_search(args, http_arg_compare, "target", tommy_strhash_u32(0, "target"));
	if (!harg)
	{
		carglog(carg, L_WARN, "no arg 'target' in query '%s'\n", http_data->uri);
		n = snprintf(errbuf, sizeof(errbuf), "no arg 'target' in query '%s'", http_data->uri);
		if (n < 0)
			n = 0;
		if ((size_t)n >= sizeof(errbuf))
			n = (int)sizeof(errbuf) - 1;
		probe_reply_error(response, carg, errbuf, (size_t)n);
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
		n = snprintf(errbuf, sizeof(errbuf), "no such module '%s' in query'%s'", module, http_data->uri);
		if (n < 0)
			n = 0;
		if ((size_t)n >= sizeof(errbuf))
			n = (int)sizeof(errbuf) - 1;
		probe_reply_error(response, carg, errbuf, (size_t)n);
		http_args_free(args);
		return;
	}

	char *hostname = NULL;
	harg = alligator_ht_search(args, http_arg_compare, "hostname", tommy_strhash_u32(0, "hostname"));
	if (harg)
		hostname = harg->value;

	if (pn->prober == APROTO_RESOLVER) {
		if (pn->query_name) {
			if (strstr(target, "://"))
				url_size = snprintf(url, sizeof(url), "%s", target);
			else if (strchr(target, '/'))
				url_size = snprintf(url, sizeof(url), "udp://%s", target);
			else if (strchr(target, ':'))
				url_size = snprintf(url, sizeof(url), "udp://%s", target);
			else
				url_size = snprintf(url, sizeof(url), "udp://%s:53", target);
		} else {
			url_size = snprintf(url, sizeof(url), "%s", pn->url ? pn->url : (pn->scheme ? pn->scheme : "resolver://"));
		}
	} else
		url_size = snprintf(url, sizeof(url), "%s%s", pn->scheme ? pn->scheme : "", target);
	if (url_size >= sizeof(url))
		url_size = sizeof(url) - 1;

	host_aggregator_info *hi = parse_url(url, url_size);
	context_arg *new_carg;
	if (pn->prober == APROTO_RESOLVER)
	{
		const char *qname = pn->query_name ? pn->query_name : target;
		json_t *dns_root = json_object();
		json_object_set_new(dns_root, "resolve", json_string(qname));
		json_object_set_new(dns_root, "type", json_string(pn->query_type ? pn->query_type : "a"));
		if (pn->name)
			json_object_set_new(dns_root, "name", json_string(pn->name));
		new_carg = context_arg_json_fill(dns_root, hi, dns_handler, "dns_handler", NULL, 0, NULL, NULL, 0, carg->loop, pn->env, pn->follow_redirects, NULL, 0);
		json_decref(dns_root);
	}
	else if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
	{
		string *body = NULL;
		if (pn->body)
			body = string_init_dup(pn->body);
		char *http_host = hostname ? hostname : hi->host;
		char *http_query = gen_http_query(pn->method, hi->query, NULL, http_host, "alligator", hi->auth, NULL, pn->env, NULL, body);
		if (body)
			string_free(body);
		new_carg = context_arg_json_fill(NULL, hi, blackbox_null, "blackbox_null", http_query, 0, NULL, NULL, 0, carg->loop, pn->env, pn->follow_redirects, NULL, 0);
	}
	else
	{
		new_carg = context_arg_json_fill(NULL, hi, blackbox_null, "blackbox_null", hi->query, 0, NULL, NULL, 0, carg->loop, pn->env, pn->follow_redirects, NULL, 0);
	}

	if (!new_carg->name && pn->name)
		new_carg->name = strdup(pn->name);

	uint64_t kick_timeout = probe_scrape_timeout_ms(http_data, pn->timeout);
	if (kick_timeout)
		new_carg->timeout = kick_timeout;

	if (pn->source_ip_address && !new_carg->bind_address)
		new_carg->bind_address = strdup(pn->source_ip_address);

	new_carg->ip_version = pn->probe_ip_version[0];
	probe_copy_opts_to_carg(new_carg, pn);

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
	if (hostname) {
		if (new_carg->tls_server_name)
			free(new_carg->tls_server_name);
		new_carg->tls_server_name = strdup(hostname);
	}

	if (pn->labels)
	{
		alligator_ht *labels = labels_dup(pn->labels);
		probe_labels_subst_target(labels, target);
		new_carg->labels = labels;
	}

	if (pn->metricstransform)
		new_carg->metricstransform = json_incref(pn->metricstransform);

	if (pn->loop)
		new_carg->pingloop = pn->loop;

	if (pn->loop)
		new_carg->pingpercent_success = pn->percent_success;

	new_carg->tls_verify = pn->tls_verify;

	probe_assign_unique_key(new_carg, module);

	if (smart_aggregator(new_carg))
		aggregator_oneshot_start(new_carg);
	else
		carg_free(new_carg);

	metric_query_context *mqc = query_context_new(NULL);
	query_context_set_label(mqc, "host", target);
	action_node an_stack;
	action_node *an = NULL;
	alligator_ht *export_labels = NULL;
	if ((pn->labels && alligator_ht_count(pn->labels)) || pn->metricstransform ||
	    (pn->metric_name_transform_pattern && pn->metric_name_transform_replacement))
	{
		memset(&an_stack, 0, sizeof(an_stack));
		if (pn->labels) {
			export_labels = labels_dup(pn->labels);
			probe_labels_subst_target(export_labels, target);
			an_stack.labels = export_labels;
		}
		an_stack.metricstransform = pn->metricstransform;
		an_stack.metric_name_transform_pattern = pn->metric_name_transform_pattern;
		an_stack.metric_name_transform_replacement = pn->metric_name_transform_replacement;
		an_stack.metric_name_transform_compiled = pn->metric_name_transform_compiled;
		an = &an_stack;
	}
	string *body = metric_query_deserialize(response->m, mqc, METRIC_SERIALIZER_OPENMETRICS, 0, NULL, NULL, NULL, NULL, an);
	if (an)
		pn->metric_name_transform_compiled = an_stack.metric_name_transform_compiled;
	query_context_free(mqc);
	if (export_labels)
		labels_hash_free(export_labels);

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
