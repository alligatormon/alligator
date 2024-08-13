#include "main.h"
#include "cluster/type.h"
#include "cluster/get.h"
#include "events/context_arg.h"
#include "metric/metrictree.h"
#include "metric/labels.h"
#include "common/selector.h"
#include "common/http.h"
#include "common/logs.h"
#include "parsers/multiparser.h"

void cluster_recurse(void *funcarg, void* arg)
{
	context_arg *carg = arg;

	if (!carg->cluster)
		return;

	cluster_node* cn = get_cluster_node_from_carg(carg);

	uint64_t servers_size = cn->servers_size;
	for (uint64_t i = 0; i < servers_size; i++)
	{
		if (cn->servers[i].is_me)
			continue;
		if (!strcmp(carg->instance, cn->servers[i].name)) {
			cn->servers[i].is_me = 1;
			continue;
		}

		string *url = string_new();
		string_cat(url, "http://", 7);
		string_cat(url, cn->servers[i].name, strlen(cn->servers[i].name));
		string_cat(url, "/oplog?name=", 12);
		string_cat(url, cn->name, strlen(cn->name));
		string_cat(url, "&replica=", 9);
		string_cat(url, carg->instance, strlen(carg->instance));
		host_aggregator_info *hi = parse_url(url->s, url->l);
		carglog(carg, L_DEBUG, "cluster sync url %s\n", url->s);
		char *query = gen_http_query(HTTP_POST, hi->query, NULL, hi->host, "alligator", hi->auth, NULL, NULL, NULL, NULL);
		context_arg *new = aggregator_oneshot(NULL, url->s, url->l, query, strlen(query), cluster_sync_handler, "cluster_sync_handler", NULL, NULL, 0, &cn->servers[i], NULL, 0, NULL, NULL);
		if (new) {
			new->period = ac->cluster_repeat;
			new->context_ttl = 0; // not a oneshot
			new->cluster_node = cn;
		}
		string_free(url);
		url_free(hi);
	}
}

void cluster_aggregate_timer_recurse(uv_timer_t *handle)
{
	context_arg *carg = handle->data;

	uv_timer_stop(handle);
	alligator_cache_push(ac->uv_cache_timer, handle);
	r_time time_now = setrtime();

	if (!carg->cluster || !carg->instance)
		return;

	cluster_node* cn = get_cluster_node_from_carg(carg);

	if ((!cn->parser_status) && (time_now.sec < cn->ttl))
		return;

	uint64_t servers_size = cn->servers_size;
	for (uint64_t i = 0; i < servers_size; i++)
	{
		string *url = string_new();
		string_cat(url, "http://", 7);
		string_cat(url, cn->servers[i].name, strlen(cn->servers[i].name));
		string_cat(url, "/sharedlock?name=", 17);
		string_cat(url, cn->name, strlen(cn->name));
		string_cat(url, "&replica=", 9);
		string_cat(url, carg->instance, strlen(carg->instance));
		host_aggregator_info *hi = parse_url(url->s, url->l);
		char *query = gen_http_query(HTTP_POST, hi->query, NULL, hi->host, "alligator", hi->auth, NULL, NULL, NULL, NULL);
		context_arg *carg = aggregator_oneshot(NULL, url->s, url->l, query, strlen(query), cluster_aggregate_sync_handler, "cluster_aggregate_sync_handler", NULL, NULL, 0, NULL, NULL, 0, NULL, NULL);
		if (carg)
			carg->data = cn;
		string_free(url);
		url_free(hi);
	}
}

void cluster_aggregate_recurse(void *funcarg, void* arg)
{
	context_arg *carg = arg;
	uv_timer_t *timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
	timer->data = carg;
	uv_timer_init(carg->loop, timer);
	uv_timer_start(timer, cluster_aggregate_timer_recurse, 0, 0);
}

void cluster_sync(uv_timer_t* handle) {
	(void)handle;
	if (ac->cluster)
		if (alligator_ht_count(ac->cluster))
		{
			alligator_ht_foreach_arg(ac->entrypoints, cluster_recurse, NULL);
			alligator_ht_foreach_arg(ac->aggregator, cluster_aggregate_recurse, NULL);
		}
}

void cluster_handler()
{
	uv_loop_t *loop = ac->loop;

	uv_timer_init(loop, &ac->cluster_timer);
	uv_timer_start(&ac->cluster_timer, cluster_sync, ac->cluster_startup, ac->cluster_reload);
}

void cluster_handler_stop()
{
	uv_timer_stop(&ac->cluster_timer);
}
