#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include "kubernetes_watch.h"
#include "kubernetes_common.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "events/client.h"
#include "events/context_arg.h"
#include "main.h"

#define KUBE_WATCH_KEY "kubernetes_operator_watch"
#define KUBE_WATCH_TCP_TIMEOUT_MS ((KUBE_WATCH_TIMEOUT_SEC + 120) * 1000)

typedef struct kubernetes_watch_state {
	kubernetes_operator_opts *opts;
	char *resource_version;
	char *mesg;
	string *pending;
	alligator_ht *env;
	context_arg *carg;
	uv_timer_t *reconnect_timer;
} kubernetes_watch_state;

static kubernetes_watch_state *kube_watch;

static void kubernetes_watch_reconnect(uv_timer_t *timer);

static size_t kube_json_next_object(const char *buf, size_t len, size_t *out_len)
{
	size_t i = 0;

	while (i < len && (buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t'))
		i++;

	if (i >= len || buf[i] != '{')
		return 0;

	int depth = 0;
	uint8_t in_string = 0;

	for (size_t j = i; j < len; j++)
	{
		char c = buf[j];

		if (in_string)
		{
			if (c == '\\' && j + 1 < len)
				j++;
			else if (c == '"')
				in_string = 0;
			continue;
		}

		if (c == '"')
			in_string = 1;
		else if (c == '{')
			depth++;
		else if (c == '}')
		{
			depth--;
			if (depth == 0)
			{
				*out_len = j - i + 1;
				return i;
			}
		}
	}

	return 0;
}

static void kubernetes_watch_update_rv(json_t *object)
{
	if (!object || !kube_watch)
		return;

	json_t *metadata = json_object_get(object, "metadata");
	json_t *rv_json = metadata ? json_object_get(metadata, "resourceVersion") : NULL;
	char *rv = (char *)json_string_value(rv_json);
	if (!rv || !rv[0])
		return;

	free(kube_watch->resource_version);
	kube_watch->resource_version = strdup(rv);
}

static void kubernetes_watch_handle_event(json_t *event, context_arg *carg)
{
	const char *type = json_string_value(json_object_get(event, "type"));
	json_t *object = json_object_get(event, "object");
	kubernetes_operator_opts *opts = kube_watch ? kube_watch->opts : NULL;

	if (!type || !object)
		return;

	kubernetes_watch_update_rv(object);

	if (!strcmp(type, "BOOKMARK"))
		return;

	if (!strcmp(type, "DELETED"))
		kubernetes_reconcile_pod_deleted(object, carg, opts);
	else if (!strcmp(type, "ADDED") || !strcmp(type, "MODIFIED"))
		kubernetes_reconcile_pod(object, carg, opts);
}

static void kubernetes_watch_consume_body(kubernetes_watch_state *st, context_arg *carg)
{
	for (;;)
	{
		size_t obj_len = 0;
		size_t offset = kube_json_next_object(st->pending->s, st->pending->l, &obj_len);
		if (!obj_len)
			break;

		json_error_t error;
		json_t *event = json_loadb(st->pending->s + offset, obj_len, 0, &error);
		if (!event)
		{
			if (carg->log_level > 0)
				fprintf(stderr, "kubernetes watch json error: %s\n", error.text);
			break;
		}

		kubernetes_watch_handle_event(event, carg);
		json_decref(event);

		size_t consumed = offset + obj_len;
		if (consumed >= st->pending->l)
		{
			string_null(st->pending);
			break;
		}

		memmove(st->pending->s, st->pending->s + consumed, st->pending->l - consumed);
		st->pending->l -= consumed;
		st->pending->s[st->pending->l] = '\0';
	}
}

static void kubernetes_watch_stream_cb(context_arg *carg, const char *data, size_t len)
{
	kubernetes_watch_state *st = kube_watch;
	if (!st || !st->pending || !len)
		return;

	string_cat(st->pending, (char *)data, len);
	kubernetes_watch_consume_body(st, carg);
}

static void kubernetes_watch_close_cb(context_arg *carg)
{
	kubernetes_watch_state *st = kube_watch;
	if (!st)
		return;

	carg->http_stream_offset = 0;
	if (st->pending)
		string_null(st->pending);

	if (carg->log_level > 0)
		printf("kubernetes watch: connection closed, reconnecting (resourceVersion=%s)\n",
			st->resource_version ? st->resource_version : "");

	if (!st->reconnect_timer)
	{
		st->reconnect_timer = calloc(1, sizeof(uv_timer_t));
		uv_timer_init(ac->loop, st->reconnect_timer);
		st->reconnect_timer->data = st;
	}

	uv_timer_stop(st->reconnect_timer);
	uv_timer_start(st->reconnect_timer, kubernetes_watch_reconnect, 500, 0);
}

static void kubernetes_watch_set_mesg(context_arg *carg, kubernetes_watch_state *st)
{
	char path[1024];
	kubernetes_pods_watch_path(path, sizeof(path), st->opts, st->resource_version);

	char *query = gen_http_query(0, NULL, path, carg->host, "alligator", NULL, NULL, st->env, NULL, NULL);
	if (!query)
		return;

	free(st->mesg);
	st->mesg = strdup(query);
	aconf_mesg_set(carg, st->mesg, strlen(st->mesg));
	free(query);
}

static void kubernetes_watch_reconnect(uv_timer_t *timer)
{
	kubernetes_watch_state *st = timer->data;
	context_arg *carg = st ? st->carg : NULL;

	if (!carg || carg->lock)
		return;

	kubernetes_watch_set_mesg(carg, st);
	string_null(carg->full_body);
	carg->http_stream_offset = 0;
	tcp_client_connect(carg);
}

static void kubernetes_watch_connect(context_arg *carg)
{
	kubernetes_watch_state *st = kube_watch;
	if (!st || !carg)
		return;

	if (carg->lock)
		return;

	kubernetes_watch_set_mesg(carg, st);
	string_null(carg->full_body);
	carg->http_stream_offset = 0;
	tcp_client_connect(carg);
}

uint8_t kubernetes_watch_active(void)
{
	return kube_watch && kube_watch->carg;
}

void kubernetes_watch_start(host_aggregator_info *hi, kubernetes_operator_opts *opts)
{
	if (!opts || !opts->watch)
		return;

	const char *token_path = "/var/run/secrets/kubernetes.io/serviceaccount/token";
	FILE *fd = fopen(token_path, "r");
	if (!fd)
		return;
	fclose(fd);

	if (!kube_watch)
	{
		kube_watch = calloc(1, sizeof(*kube_watch));
		kube_watch->pending = string_init(65536);
	}

	kube_watch->opts = opts;

	if (!kube_watch->env)
		kube_watch->env = kubernetes_incluster_env(NULL);

	if (kube_watch->carg)
	{
		kube_watch->carg->data = opts;
		if (!kube_watch->carg->lock)
			kubernetes_watch_connect(kube_watch->carg);
		return;
	}

	context_arg *carg = context_arg_json_fill(NULL, hi, NULL, "kubernetes_watch", NULL, 0, opts, NULL, 0,
		ac->loop, kube_watch->env, 0, NULL, 0);
	if (!carg)
		return;

	carg->key = strdup(KUBE_WATCH_KEY);
	carg->context_ttl = 0;
	carg->timeout = KUBE_WATCH_TCP_TIMEOUT_MS;
	carg->period = 0;
	carg->no_metric = 1;
	carg->http_stream_body = 1;
	carg->http_stream_body_cb = kubernetes_watch_stream_cb;
	carg->http_stream_close_cb = kubernetes_watch_close_cb;

	if (!smart_aggregator(carg))
	{
		carg_free(carg);
		return;
	}

	kube_watch->carg = carg;
	kubernetes_watch_connect(carg);
}
