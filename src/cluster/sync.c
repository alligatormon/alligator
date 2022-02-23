#include "main.h"
#include "cluster/type.h"
#include "cluster/get.h"
#include "events/context_arg.h"
#include "metric/metrictree.h"
#include "metric/labels.h"
#include "common/selector.h"
#include "common/http.h"
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
		string *url = string_new();
		string_cat(url, "http://", 7);
		string_cat(url, cn->servers[i].name, strlen(cn->servers[i].name));
		string_cat(url, "/oplog?name=", 12);
		string_cat(url, cn->name, strlen(cn->name));
		string_cat(url, "&replica=", 9);
		string_cat(url, carg->instance, strlen(carg->instance));
		host_aggregator_info *hi = parse_url(url->s, url->l);
		char *query = gen_http_query(HTTP_POST, hi->query, NULL, hi->host, "alligator", hi->auth, 0, NULL, NULL, NULL, NULL);
		aggregator_oneshot(NULL, url->s, url->l, strdup(query), strlen(query), cluster_sync_handler, "cluster_sync_handler", NULL, NULL, 0, NULL, NULL, 0);
		url_free(hi);
	}
}

void cluster_sync(uv_timer_t* handle) {
	(void)handle;
	if (ac->cluster)
		if (alligator_ht_count(ac->cluster))
			alligator_ht_foreach_arg(ac->entrypoints, cluster_recurse, NULL);
}

void cluster_handler()
{
	uv_loop_t *loop = ac->loop;

	uv_timer_init(loop, &ac->cluster_timer);
	uv_timer_start(&ac->cluster_timer, cluster_sync, ac->cluster_startup, ac->cluster_repeat);
}

void cluster_handler_stop()
{
	uv_timer_stop(&ac->cluster_timer);
}
