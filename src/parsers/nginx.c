#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "common/selector.h"
#include "main.h"

void nginx_handler(char *metrics, size_t size, context_arg *carg)
{
	if (!metrics || !size) {
		carg->parser_status = 0;
		return;
	}

	namespace_metric_family_set(NULL, carg, "nginx_connections", METRIC_TYPE_GAUGE, "Nginx stub_status connections by state.");
	namespace_metric_family_set(NULL, carg, "nginx_accepts_total", METRIC_TYPE_COUNTER, "Nginx accepted client connections.");
	namespace_metric_family_set(NULL, carg, "nginx_handled_total", METRIC_TYPE_COUNTER, "Nginx handled connections.");
	namespace_metric_family_set(NULL, carg, "nginx_requests_total", METRIC_TYPE_COUNTER, "Nginx requests.");

	int64_t active = 0, reading = 0, writing = 0, waiting = 0;
	int64_t accepts = 0, handled = 0, requests = 0;
	int got_active = 0, got_counters = 0;

	char *copy = strndup(metrics, size);
	if (!copy) {
		carg->parser_status = 0;
		return;
	}
	char *save = NULL;
	for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		if (sscanf(line, "Active connections: %" SCNd64, &active) == 1)
			got_active = 1;
		else if (sscanf(line, " %" SCNd64 " %" SCNd64 " %" SCNd64, &accepts, &handled, &requests) == 3)
			got_counters = 1;
		else
			sscanf(line, "Reading: %" SCNd64 " Writing: %" SCNd64 " Waiting: %" SCNd64,
				&reading, &writing, &waiting);
	}
	free(copy);

	if (!got_active && !got_counters) {
		carg->parser_status = 0;
		return;
	}

	metric_add_labels("nginx_connections", &active, DATATYPE_INT, carg, "state", "active");
	metric_add_labels("nginx_connections", &reading, DATATYPE_INT, carg, "state", "reading");
	metric_add_labels("nginx_connections", &writing, DATATYPE_INT, carg, "state", "writing");
	metric_add_labels("nginx_connections", &waiting, DATATYPE_INT, carg, "state", "waiting");
	metric_add_auto("nginx_accepts_total", &accepts, DATATYPE_INT, carg);
	metric_add_auto("nginx_handled_total", &handled, DATATYPE_INT, carg);
	metric_add_auto("nginx_requests_total", &requests, DATATYPE_INT, carg);
	carg->parser_status = 1;
}

string* nginx_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	(void)arg;
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		return string_init_add_auto(gen_http_query(0, hi->query, NULL, hi->host, "alligator", hi->auth, "1.1", env, proxy_settings, NULL));
	return NULL;
}

void nginx_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));
	actx->key = strdup("nginx");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);
	actx->handler[0].name = nginx_handler;
	actx->handler[0].mesg_func = nginx_mesg;
	strlcpy(actx->handler[0].key, "nginx", 255);
	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
