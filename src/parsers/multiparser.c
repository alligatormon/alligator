#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <openssl/crypto.h>
#include "main.h"
#include "common/auth.h"
#include "common/authorize.h"
#include "common/http_limit.h"
#include "parsers/multiparser.h"
#include "parsers/pushgateway.h"
#include "common/selector.h"
#include "common/http.h"
#include "common/http_entrypoint.h"
#include "router/router.h"
#include "common/logs.h"
#define READY_HANDLER "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\n"
#define HANDLER_202 "HTTP/1.1 202 Accepted\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\n"
#define HANDLER_400 "HTTP/1.1 400 Bad Request\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"
#define HTTP_NOT_FOUND "HTTP/1.1 404 Not Found\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"
#define HTTP_INTERNAL_ERROR "HTTP/1.1 500 Internal Server Error\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"
#define HANDLER_415 "HTTP/1.1 415 Unsupported Media Type\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"
#define OPTIONS_ANSWER "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\nAllow: OPTIONS, GET, PUT, POST\r\n"
#define METHOD_NOT_ALLOWED "HTTP/1.1 405 Method Not Allowed\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\n"
#define COMMON_ANSWER "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"
#define OPENMETRICS_ANSWER "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: application/openmetrics-text; version=1.0.0; charset=utf-8\r\nConnection: close\r\n"
#define HTTP_FORBIDDEN "HTTP/1.1 403 Access Forbidden\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"
#define HTTP_UNAUTHORIZED "HTTP/1.1 401 Unauthorized\r\nServer: alligator\r\nContent-Type: text/plain\r\nConnection: close\r\n"

typedef struct prometheus_metrics_body_ctx {
	string *body;
	int openmetrics;
	context_arg *carg;
} prometheus_metrics_body_ctx;

static const char *http_method_str(uint8_t method)
{
	switch (method) {
	case HTTP_METHOD_GET: return "GET";
	case HTTP_METHOD_POST: return "POST";
	case HTTP_METHOD_PUT: return "PUT";
	case HTTP_METHOD_DELETE: return "DELETE";
	case HTTP_METHOD_OPTIONS: return "OPTIONS";
	case HTTP_METHOD_HEAD: return "HEAD";
	case HTTP_METHOD_CONNECT: return "CONNECT";
	case HTTP_METHOD_TRACE: return "TRACE";
	case HTTP_METHOD_PATCH: return "PATCH";
	case HTTP_METHOD_RESPONSE: return "RESPONSE";
	default: return "UNKNOWN";
	}
}

static char* find_sep(const char *buf, size_t len, const char *sep, size_t sep_len)
{
	if (!buf || !sep || !sep_len || len < sep_len)
		return NULL;

	size_t last = len - sep_len;
	for (size_t i = 0; i <= last; ++i)
	{
		if (!memcmp(buf + i, sep, sep_len))
			return (char*)(buf + i);
	}

	return NULL;
}

void do_http_post(char *buf, size_t len, string *response, http_reply_data* http_data, context_arg *carg)
{
	char *body = http_data->body;
	char *uri = http_data->uri;
	size_t body_len = http_data->body_size;

	if (!body_len && body)
		body_len = strlen(body);

	if (!strncmp(http_data->uri, "/api", 4))
	{
		api_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/v1/metrics", 11))
	{
		otlp_metrics_ingest_handler(response, http_data, NULL, carg);
	}
	else if (!strncmp(http_data->uri, "/oplog", 6))
	{
		oplog_post_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/sharedlock", 11))
	{
		sharedlock_post_router(response, http_data, carg);
	}
	else
	{
		carg_or_glog(carg, L_DEBUG,
			"http push metrics: method=%s uri=%s content_length=%" PRId64 " body_size=%zu key=%s parser=%s pquery=%u host=%s\n",
			http_method_str(http_data->method),
			uri ? uri : "-",
			http_data->content_length,
			body_len,
			carg && carg->key ? carg->key : "-",
			carg && carg->parser_name ? carg->parser_name : "-",
			carg ? (unsigned)carg->pquery_size : 0u,
			carg && carg->host[0] ? carg->host : "-");
		if (body && body_len)
			carg_or_glog(carg, L_TRACE,
				"http push metrics body (%zu bytes, preview up to 4096):\n%.*s\n",
				body_len,
				(int)(body_len > 4096 ? 4096 : body_len),
				body);
		multicollector(http_data, NULL, 0, carg);
		if (carg->pg_http_status == 400)
			string_cat(response, HANDLER_400, strlen(HANDLER_400));
		else if (carg->pg_http_status == 415)
			string_cat(response, HANDLER_415, strlen(HANDLER_415));
		else
			string_cat(response, HANDLER_202, strlen(HANDLER_202));

		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		http_entrypoint_finish_empty_body(response);
	}
}

void do_http_options(char *buf, size_t len, string *response, http_reply_data* http_data, context_arg *carg)
{
	string_cat(response, OPTIONS_ANSWER, strlen(OPTIONS_ANSWER));
	if (carg->env)
		alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
	http_entrypoint_finish_empty_body(response);
}

void do_http_not_allowed(char *buf, size_t len, string *response, http_reply_data* http_data, context_arg *carg)
{
	string_cat(response, METHOD_NOT_ALLOWED, strlen(METHOD_NOT_ALLOWED));
	if (carg->env)
		alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
	http_entrypoint_finish_empty_body(response);
}

void do_http_delete(char *buf, size_t len, string *response, http_reply_data* http_data, context_arg *carg)
{
	if (!strncmp(http_data->uri, "/api", 4))
	{
		api_router(response, http_data, carg);
		return;
	}

	/* Pushgateway-compatible DELETE /metrics/job/<job>[/<label>/<value>...], selector semantics */
	size_t uri_len = http_data->uri_size ? http_data->uri_size : (http_data->uri ? strlen(http_data->uri) : 0);
	int st = pushgateway_delete(http_data->uri, uri_len, carg);
	if (st == 202)
		string_cat(response, HANDLER_202, strlen(HANDLER_202));
	else if (st == 400)
		string_cat(response, HANDLER_400, strlen(HANDLER_400));
	else if (st == 500)
		string_cat(response, HTTP_INTERNAL_ERROR, strlen(HTTP_INTERNAL_ERROR));
	else
		string_cat(response, HTTP_NOT_FOUND, strlen(HTTP_NOT_FOUND));

	if (carg->env)
		alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
	http_entrypoint_finish_empty_body(response);
}

void prometheus_response_cluster_namespaces(void *funcarg, void* arg)
{
	cluster_node *cn = arg;
	prometheus_metrics_body_ctx *ctx = funcarg;
	string *body = ctx->body;
	int openmetrics = ctx->openmetrics;
	char namespacename[255];
	r_time time_now = setrtime();

	/*
	 * Replica namespaces (`cluster:primary_peer`) are HA copies. Export them
	 * only when that peer's sync TTL has expired (peer looks dead), so a live
	 * primary is not double-scraped by Prometheus. Healthy cluster → only the
	 * Maglev primary answers each series from nsdefault.
	 */
	for (uint64_t i = 0; i < cn->servers_size; i++)
	{
		uint16_t cur;

		if (cn->servers[i].is_me)
			continue;
		if (!cn->name || !cn->servers[i].name)
			continue;
		if (cn->servers[i].ttl >= time_now.sec)
			continue;

		cur = strlcpy(namespacename, cn->name, sizeof(namespacename));
		if (cur >= sizeof(namespacename) - 2)
			continue;
		namespacename[cur++] = ':';
		strlcpy(namespacename + cur, cn->servers[i].name, sizeof(namespacename) - cur);

		metric_str_build(namespacename, body, openmetrics, ctx->carg);
	}
}

static int http_authorize_name_ok(const char *s, int cookie)
{
	if (!s || !s[0])
		return 0;
	if (strpbrk(s, "\r\n"))
		return 0;
	if (cookie && strpbrk(s, ";= \t"))
		return 0;
	if (!cookie && strpbrk(s, ": \t"))
		return 0;
	return 1;
}

static void http_authorize_mint(string *response, context_arg *carg, http_reply_data *http_data)
{
	char hex[AUTHORIZE_HEX_LEN + 1];
	uint64_t now;
	uint64_t bucket;
	const char *cookie_name;
	const char *header_name;
	char *values[AUTHORIZE_BIND_MAX];
	const char *const *bound = NULL;
	int filled = AUTHORIZE_FACTORS_OK;
	uint8_t fail_index = 0;

	memset(values, 0, sizeof(values));
	if (carg->authorize_config_error)
	{
		carglog(carg, L_ERROR, "authorize: bind config rejected\n");
		string_cat(response, HTTP_INTERNAL_ERROR, strlen(HTTP_INTERNAL_ERROR));
		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		http_entrypoint_finish_empty_body(response);
		return;
	}
	if (carg->authorize_bind_count)
	{
		char label[160];

		filled = authorize_factors_fill(carg, http_data, values, &fail_index);
		if (filled != AUTHORIZE_FACTORS_OK)
		{
			if (carg->authorize_binds && fail_index < carg->authorize_bind_count &&
			    authorize_bind_label(&carg->authorize_binds[fail_index], label, sizeof(label)) > 0)
				carglog(carg, L_ERROR, "authorize: bind %s missing\n", label);
			else
				carglog(carg, L_ERROR, "authorize: bind missing\n");
			if (filled == AUTHORIZE_FACTORS_PEER)
				string_cat(response, HTTP_INTERNAL_ERROR, strlen(HTTP_INTERNAL_ERROR));
			else
				string_cat(response, HANDLER_400, strlen(HANDLER_400));
			if (carg->env)
				alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
			http_entrypoint_finish_empty_body(response);
			return;
		}
		bound = (const char *const *)values;
	}

	now = (uint64_t)time(NULL);
	bucket = now / carg->authorize_period;
	if (!authorize_compute_bound(carg->authorize_secret, bucket, carg->authorize_binds, carg->authorize_bind_count, bound, hex))
	{
		authorize_factors_free(values, carg->authorize_bind_count);
		string_cat(response, HTTP_INTERNAL_ERROR, strlen(HTTP_INTERNAL_ERROR));
		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		http_entrypoint_finish_empty_body(response);
		return;
	}

	cookie_name = carg->authorize_cookie_name ? carg->authorize_cookie_name : "alligator_auth";
	header_name = carg->authorize_header_name ? carg->authorize_header_name : "X-Alligator-Auth";

	if (carg->authorize_set == AUTHORIZE_SET_JSON)
	{
		char body[192];
		int n;
		uint64_t expires = (bucket + 1) * carg->authorize_period;

		string_cat(response, "HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\n",
			strlen("HTTP/1.1 200 OK\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\n"));
		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		n = snprintf(body, sizeof(body),
			"{\"secret\":\"%s\",\"period\":%" PRIu64 ",\"expires\":%" PRIu64 "}",
			hex, carg->authorize_period, expires);
		if (n < 0 || (size_t)n >= sizeof(body))
			n = 0;
		http_entrypoint_finish_body(response, body, (size_t)n);
		OPENSSL_cleanse(body, sizeof(body));
	}
	else if (carg->authorize_set == AUTHORIZE_SET_HEADER)
	{
		if (!http_authorize_name_ok(header_name, 0))
		{
			carglog(carg, L_ERROR, "authorize: header name rejected\n");
			string_cat(response, HTTP_INTERNAL_ERROR, strlen(HTTP_INTERNAL_ERROR));
			if (carg->env)
				alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
			http_entrypoint_finish_empty_body(response);
		}
		else
		{
			size_t need = strlen(header_name) + AUTHORIZE_HEX_LEN + 8;
			char *line = malloc(need);

			string_cat(response, COMMON_ANSWER, strlen(COMMON_ANSWER));
			if (line)
			{
				snprintf(line, need, "%s: %s\r\n", header_name, hex);
				string_cat(response, line, strlen(line));
				OPENSSL_cleanse(line, need);
				free(line);
			}
			if (carg->env)
				alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
			http_entrypoint_finish_empty_body(response);
		}
	}
	else
	{
		if (!http_authorize_name_ok(cookie_name, 1))
		{
			carglog(carg, L_ERROR, "authorize: cookie name rejected\n");
			string_cat(response, HTTP_INTERNAL_ERROR, strlen(HTTP_INTERNAL_ERROR));
			if (carg->env)
				alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
			http_entrypoint_finish_empty_body(response);
		}
		else
		{
			size_t need = strlen(cookie_name) + AUTHORIZE_HEX_LEN + 64;
			char *line = malloc(need);

			string_cat(response, COMMON_ANSWER, strlen(COMMON_ANSWER));
			if (line)
			{
				snprintf(line, need, "Set-Cookie: %s=%s; Path=/; Max-Age=%" PRIu64 "; HttpOnly\r\n",
					cookie_name, hex, carg->authorize_period);
				string_cat(response, line, strlen(line));
				OPENSSL_cleanse(line, need);
				free(line);
			}
			if (carg->env)
				alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
			http_entrypoint_finish_empty_body(response);
		}
	}

	authorize_factors_free(values, carg->authorize_bind_count);
	OPENSSL_cleanse(hex, sizeof(hex));
}

void do_http_get(char *buf, size_t len, string *response, http_reply_data* http_data, context_arg *carg)
{
	if (http_data->uri && http_uri_is_authorize(http_data->uri) && carg &&
	    carg->authorize_secret && carg->authorize_secret[0] && carg->authorize_period)
	{
		http_authorize_mint(response, carg, http_data);
	}
	else if (!strncmp(http_data->uri, "/api", 4))
	{
		api_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/stats", 6))
	{
		stat_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/probe", 6))
	{
		probe_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/json", 5))
	{
		json_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/dsv", 4))
	{
		dsv_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/conf", 5))
	{
		conf_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/labels_cache", 13))
	{
		labels_cache_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/oplog", 6))
	{
		oplog_get_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/sharedlock", 11))
	{
		sharedlock_get_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/resolver", 9))
	{
		resolver_router(response, http_data, carg);
	}
	else if (!strncmp(http_data->uri, "/ready", 6))
	{
		string_cat(response, READY_HANDLER, strlen(READY_HANDLER));
		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		http_entrypoint_finish_empty_body(response);
	}
	else
	{
		string *body = string_init(response->m);

		alligator_ht *args = http_get_args(http_data->uri, http_data->uri_size);
		char *namespace = http_get_param(args, "namespace");
		int openmetrics = (carg && carg->metrics_openmetrics_set) ? carg->metrics_openmetrics : 1;
		char *format = http_get_param(args, "format");
		char *omp = http_get_param(args, "openmetrics");

		if (format && !strcmp(format, "prometheus"))
			openmetrics = 0;
		else if (format && !strcmp(format, "openmetrics"))
			openmetrics = 1;
		if (omp && !strcmp(omp, "0"))
			openmetrics = 0;
		if (omp && !strcmp(omp, "1"))
			openmetrics = 1;

		prometheus_metrics_body_ctx body_ctx = { .body = body, .openmetrics = openmetrics, .carg = carg };
		if (!namespace)
			alligator_ht_foreach_arg(ac->cluster, prometheus_response_cluster_namespaces, &body_ctx);

		metric_str_build(namespace, body, openmetrics, carg);
		if (openmetrics)
			string_cat(body, "# EOF\n", 6);

		char *content_length = malloc(255);
		snprintf(content_length, 255, "Content-Length: %zu\r\n\r\n", body->l);

		if (openmetrics)
			string_cat(response, OPENMETRICS_ANSWER, strlen(OPENMETRICS_ANSWER));
		else
			string_cat(response, COMMON_ANSWER, strlen(COMMON_ANSWER));
		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		string_cat(response, content_length, strlen(content_length));
		string_cat(response, body->s, body->l);

		http_args_free(args);
		free(content_length);
		string_free(body);
	}

	if (carg->rreturn == ENTRYPOINT_RETURN_EMPTY)
	{
		string_null(response);
		string_cat(response, READY_HANDLER, strlen(READY_HANDLER));
		http_entrypoint_finish_empty_body(response);
	}
}

void do_http_put(char *buf, size_t len, string *response, http_reply_data* http_data, context_arg *carg)
{
	do_http_post(buf, len, response, http_data, carg);
	//string_cat(response, "HTTP/1.1 400 Bad Query\n\n", strlen("HTTP/1.1 400 Bad Query\n\n")+1);
}

void do_http_response(char *buf, size_t len, string *response, context_arg *carg, http_reply_data* http_data)
{
	(void)response;
	char *tmp = find_sep(buf, len, "\r\n\r\n", 4);
	size_t off = 4;
	if (!tmp)
	{
		tmp = find_sep(buf, len, "\n\n", 2);
		off = 2;
	}

	if (tmp)
	{
		size_t hdr = (size_t)(tmp - buf);
		if (hdr + off <= len)
			multicollector(http_data, tmp + off, len - hdr - off, carg);
	}
}

int http_parser(char *buf, size_t len, string *response, context_arg *carg)
{
	if (!response)
		return 0;

	int ret = 1;
	size_t shift = strspn(buf, "\r\n \t");
	buf += shift;
	len -= shift;
	if (!len)
		return 0;

	http_reply_data* http_data = http_proto_get_request_data(buf, len, carg->auth_header);
	if(!http_data)
		return 0;

	http_entrypoint_negotiate(carg, http_data);

	if (carg->http_limit_config_error)
	{
		carglog(carg, L_ERROR, "http limit: config rejected\n");
		string_cat(response, HTTP_INTERNAL_ERROR, strlen(HTTP_INTERNAL_ERROR));
		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		http_entrypoint_finish_empty_body(response);
		http_reply_data_free(http_data);
		return 1;
	}

	{
		uint64_t retry_after = 1;

		if (!http_limit_check(carg, http_data, uv_hrtime(), &retry_after))
		{
			char hdr[192];
			int hdr_n;
			static char hdr_fallback[] =
				"HTTP/1.1 429 Too Many Requests\r\n"
				"Server: alligator\r\n"
				"Content-Type: text/plain\r\n"
				"Connection: close\r\n"
				"Retry-After: 1\r\n";

			hdr_n = snprintf(hdr, sizeof(hdr),
				"HTTP/1.1 429 Too Many Requests\r\n"
				"Server: alligator\r\n"
				"Content-Type: text/plain\r\n"
				"Connection: close\r\n"
				"Retry-After: %" PRIu64 "\r\n",
				retry_after ? retry_after : 1);
			if (hdr_n > 0 && (size_t)hdr_n < sizeof(hdr))
				string_cat(response, hdr, (size_t)hdr_n);
			else
				string_cat(response, hdr_fallback, sizeof(hdr_fallback) - 1);
			if (carg->env)
				alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
			http_entrypoint_finish_empty_body(response);
			http_reply_data_free(http_data);
			return 1;
		}
	}

	int8_t auth = http_check_auth(carg, http_data);
	if (auth < 1 && http_data->method != HTTP_METHOD_OPTIONS)
	{
		char *resp_code = HTTP_FORBIDDEN;
		if (auth == -1)
			resp_code = HTTP_UNAUTHORIZED;
		carglog(carg, L_ERROR, "http auth: %s %s %s%s%s\n",
			auth == -1 ? "401" : "403",
			http_method_str(http_data->method),
			http_data->uri ? http_data->uri : "-",
			http_data->auth_deny[0] ? " " : "",
			http_data->auth_deny);
		string_cat(response, resp_code, strlen(resp_code));
		if (carg->env)
			alligator_ht_foreach_arg(carg->env, env_serialize_http_answer, response);
		http_entrypoint_finish_empty_body(response);
		http_reply_data_free(http_data);
		return 1;
	}

	if (http_data->method == HTTP_METHOD_POST)
	{
		if (http_data->body)
			do_http_post(buf, len, response, http_data, carg);
	}
	else if (http_data->method == HTTP_METHOD_RESPONSE)
	{
		do_http_response(buf, len, response, carg, http_data);
	}
	else if (http_data->method == HTTP_METHOD_GET)
	{
		do_http_get(buf, len, response, http_data, carg);
	}
	else if (http_data->method == HTTP_METHOD_PUT)
	{
		if (http_data->body)
	 		do_http_put(buf, len, response, http_data, carg);
	}
	else if (http_data->method == HTTP_METHOD_DELETE)
	{
		if (http_data->body)
	 		do_http_delete(buf, len, response, http_data, carg);
	}
	else if (http_data->method == HTTP_METHOD_OPTIONS)
	{
	    do_http_options(buf, len, response, http_data, carg);
	}
	else if ((http_data->method == HTTP_METHOD_HEAD) ||
		(http_data->method == HTTP_METHOD_TRACE) ||
		(http_data->method == HTTP_METHOD_PATCH))

	{
        do_http_not_allowed(buf, len, response, http_data, carg);
	}
	else	ret = 0;

	http_reply_data_free(http_data);
	return ret;
}

int plain_parser(char *buf, size_t len, context_arg *carg)
{
	multicollector(NULL, buf, len, carg);
	return 1;
}

void alligator_multiparser(char *buf, size_t slen, void (*handler)(char*, size_t, context_arg*), string *response, context_arg *carg)
{
	if (carg)
	{
		char preview[128];
		carglog(carg, L_TRACE, "multiparser: begin handler=%p buflen=%zu\n", handler, slen);
		/* Binary HTTP/2 often has a NUL in the 24-bit length — '%s' looks empty. */
		carglog(carg, L_TRACE, "multiparser: buffer preview=%s\n",
			log_printable_preview(preview, sizeof(preview), buf, slen));
		carglog(carg, L_TRACE, "multiparser: end\n");
	}
	if (!buf)
		return;

	size_t len = slen;

	if (carg)
	{
		carg->response = response;
		carg->parsed = 1;
		carg->parser_status = 0;
		carg->exec_time = setrtime();
	}
	
	if ( handler )
	{
		char *proxybuf;
		uint64_t proxylen;
		if (carg && !carg->headers_pass && carg->is_http_query)
		{
			proxybuf = strstr(buf + carg->headers_size, "\r\n\r\n");
			if (!proxybuf)
				proxybuf = buf + carg->headers_size;

			proxybuf += strspn(proxybuf, "\r\n");
			proxylen = len - (proxybuf - buf);
		}
		else
		{
			proxybuf = buf;
			proxylen = len;
		}
		//printf(">>>>>>>>>>>>>>>\n%zu\n'%s'\n<<<<<<<<<<<<<<<<<<\n", proxylen, proxybuf);
		if (proxybuf && carg && carg->log_ch_raw && proxylen)
			carglog_raw(carg, proxybuf, proxylen);
		if (proxybuf)
			handler(proxybuf, proxylen, carg);

		if (carg)
			carg->exec_time_finish = setrtime();

		return;
	}
	int rc = 0;
	if ( (rc = http_parser(buf, len, response, carg)) ) {}
	else if ( (rc = plain_parser(buf, len, carg)) ) {}

	if (carg)
		carg->exec_time_finish = setrtime();
}

string* tcp_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if (hi->query)
		return string_init_add_auto(strdup(hi->query));
	else
		return NULL;
}

void tcp_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("tcp");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = tcp_mesg;
	strlcpy(actx->handler[0].key,"tcp", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

string* blackbox_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		return string_init_add_auto(gen_http_query(0, hi->query, "", hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
	else if (hi->query)
		return string_init_alloc(hi->query, 0);
	else
		return NULL;
}

void blackbox_null(char *metrics, size_t size, context_arg *carg)
{
	carg->parser_status = 1;
}

void blackbox_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("blackbox");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = blackbox_null;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = blackbox_mesg;
	actx->handler[0].headers_pass = 0;
	strlcpy(actx->handler[0].key,"blackbox", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void http_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("http");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = blackbox_null;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = blackbox_mesg;
	actx->handler[0].headers_pass = 0;
	strlcpy(actx->handler[0].key,"http", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void process_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("process");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key, "process", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
