#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/selector.h"
#include "main.h"

void fail2ban_handler(char *metrics, size_t size, context_arg *carg)
{
	if (!metrics || !size) {
		carg->parser_status = 0;
		return;
	}

	namespace_metric_family_set(NULL, carg, "fail2ban_failed", METRIC_TYPE_GAUGE, "Currently failed attempts in a fail2ban jail.");
	namespace_metric_family_set(NULL, carg, "fail2ban_banned", METRIC_TYPE_GAUGE, "Currently banned addresses in a fail2ban jail.");
	namespace_metric_family_set(NULL, carg, "fail2ban_jails", METRIC_TYPE_GAUGE, "Number of fail2ban jails.");

	char *copy = strndup(metrics, size);
	if (!copy) {
		carg->parser_status = 0;
		return;
	}

	char jail[128] = "";
	int64_t failed = -1, banned = -1;
	int64_t njails = -1;
	int found = 0;

	char *save = NULL;
	for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		char *p = line;
		while (*p == ' ' || *p == '|' || *p == '`' || *p == '-')
			++p;
		if (!strncmp(p, "Status for the jail:", 20)) {
			p += 20;
			p += strspn(p, " \t");
			strlcpy(jail, p, sizeof(jail));
			jail[strcspn(jail, "\r\n")] = 0;
			found = 1;
		} else if (strstr(p, "Currently failed:")) {
			char *col = strchr(p, ':');
			if (col)
				failed = atoll(col + 1);
			found = 1;
		} else if (strstr(p, "Currently banned:")) {
			char *col = strchr(p, ':');
			if (col)
				banned = atoll(col + 1);
			found = 1;
		} else if (strstr(p, "Number of jail:")) {
			char *col = strchr(p, ':');
			if (col)
				njails = atoll(col + 1);
			found = 1;
		}
	}
	free(copy);

	if (!found) {
		carg->parser_status = 0;
		return;
	}

	if (njails >= 0)
		metric_add_auto("fail2ban_jails", &njails, DATATYPE_INT, carg);
	if (jail[0] && failed >= 0)
		metric_add_labels("fail2ban_failed", &failed, DATATYPE_INT, carg, "jail", jail);
	if (jail[0] && banned >= 0)
		metric_add_labels("fail2ban_banned", &banned, DATATYPE_INT, carg, "jail", jail);
	carg->parser_status = 1;
}

string* fail2ban_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	(void)arg;
	(void)env;
	(void)proxy_settings;
	(void)hi;
	return NULL;
}

void fail2ban_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));
	actx->key = strdup("fail2ban");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);
	actx->handler[0].name = fail2ban_handler;
	actx->handler[0].mesg_func = fail2ban_mesg;
	strlcpy(actx->handler[0].key, "fail2ban", 255);
	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
