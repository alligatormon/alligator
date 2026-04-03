#include <stdio.h>
#include <string.h>
#include "common/logs.h"
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/validator.h"
#include "main.h"

void beanstalkd_handler(char *metrics, size_t size, context_arg *carg)
{
	char *tmp = strstr(metrics, "\n");
	if (!tmp)
		return;

	++tmp;
	tmp += strcspn(tmp, "\n");
	tmp += strspn(tmp, "\n");
	
	plain_parse(tmp, size, ": ", "\n", "beanstalkd_", 11, carg);
	carg->parser_status = 1;
}


// format:
// "OK 323\r\n---\nname: something\ncurrent-jobs-urgent: 0\ncurrent-jobs-ready: 0\ncurrent-jobs-reserved: 0\ncurrent-jobs-reserved-static: 0\ncurrent-jobs-delayed: 0\ncurrent-jobs-buried: 1000\ntotal-jobs: 8150629\ncurrent-using: 600\ncurrent-watching: 388\ncurrent-waiting: 87\ncmd-delete: 8149765\ncmd-pause-tube: 0\npause: 0\npause-time-left: 0\n\r\n"

void beanstalkd_stats_tube(char *metrics, size_t size, context_arg *carg)
{
	char *tmp = metrics;
	if (!tmp)
		return;

	uint64_t val = 1;

	if (strncmp(tmp, "OK", 2)) {
		carglog(carg, L_ERROR, "{\"fd\": %d, \"conn\": \"%s\", \"action\": \"poll event unexpected beanstalkd stats tube response\", \"response\": \"%s\"}\n", carg->fd, carg->key, tmp);
		carg->parser_status = 0;
		metric_add_labels2("beanstalkd_error", &val, DATATYPE_UINT, carg, "name", "beanstalkd_stats_tube", "reason", "unexpected response");
		return;
	}

	++tmp;
	tmp += strcspn(tmp, "\r\n");
	tmp += strspn(tmp, "\r\n");
	if (strncmp(tmp, "---", 3)) {
		carglog(carg, L_ERROR, "{\"fd\": %d, \"conn\": \"%s\", \"action\": \"poll event unexpected beanstalkd stats tube response\", \"response\": \"%s\"}\n", carg->fd, carg->key, tmp);
		carg->parser_status = 0;
		metric_add_labels2("beanstalkd_error", &val, DATATYPE_UINT, carg, "name", "beanstalkd_stats_tube", "reason", "unexpected response");
		return;
	}
	++tmp;
	tmp += strcspn(tmp, "\r\n");
	tmp += strspn(tmp, "\r\n");

	if (strncmp(tmp, "name:", 5)) {
		carglog(carg, L_ERROR, "{\"fd\": %d, \"conn\": \"%s\", \"action\": \"poll event unexpected beanstalkd stats tube response\", \"response\": \"%s\"}\n", carg->fd, carg->key, tmp);
		carg->parser_status = 0;
		metric_add_labels2("beanstalkd_error", &val, DATATYPE_UINT, carg, "name", "beanstalkd_stats_tube", "reason", "unexpected response");
		return;
	}

	tmp += strcspn(tmp, " :");
	tmp += strspn(tmp, " :");
	char tube_name[255];
	uint64_t tube_name_len = strcspn(tmp, "\r\n");
	strlcpy(tube_name, tmp, tube_name_len + 1);
	metric_label_value_validator_normalizer(tube_name, tube_name_len);
	tmp += tube_name_len;
	tmp += strcspn(tmp, "\r\n");
	tmp += strspn(tmp, "\r\n");

	char metric_name[255];
	strlcpy(metric_name, "beanstalkd_stats_tube_", 254);
	while (*tmp) {
		uint64_t metric_name_len = strcspn(tmp, " :\t");
		strlcpy(metric_name + 22, tmp, metric_name_len + 1);
		metric_name_normalizer(metric_name, metric_name_len + 21);
		val = strtoull(tmp, NULL, 10);
		metric_add_labels(metric_name, &val, DATATYPE_UINT, carg, "tube_name", tube_name);
		tmp += metric_name_len;
		tmp += strcspn(tmp, "\r\n");
		tmp += strspn(tmp, "\r\n");
	}
	carg->parser_status = 1;
}

// format:
// "OK 141\r\n---\n- default\n- operation_task\n- rsyslog\n- logger\n- hdd\n- cron\n- events\n- deleted\n- templates\n"

void beanstalkd_tubes_list_handler(char *metrics, size_t size, context_arg *carg)
{
	uint64_t val = 1;
	char *tmp = metrics;

	if (strncmp(tmp, "OK", 2)) {
		carglog(carg, L_ERROR, "{\"fd\": %d, \"conn\": \"%s\", \"action\": \"poll event unexpected beanstalkd tubes list response\", \"response\": \"%s\"}\n", carg->fd, carg->key, tmp);
		carg->parser_status = 0;
		metric_add_labels2("beanstalkd_error", &val, DATATYPE_UINT, carg, "name", "beanstalkd_tubes_list", "reason", "unexpected response");
		return;
	}

	++tmp;
	tmp += strcspn(tmp, "\r\n");
	tmp += strspn(tmp, "\r\n");

	if (strncmp(tmp, "---\n", 4)) {
		carglog(carg, L_ERROR, "{\"fd\": %d, \"conn\": \"%s\", \"action\": \"poll event unexpected beanstalkd tubes list response\", \"response\": \"%s\"}\n", carg->fd, carg->key, tmp);
		carg->parser_status = 0;
		metric_add_labels2("beanstalkd_error", &val, DATATYPE_UINT, carg, "name", "beanstalkd_tubes_list", "reason", "unexpected response");
		return;
	}
	++tmp;
	tmp += strcspn(tmp, "\r\n");
	tmp += strspn(tmp, "\r\n");

	while (*tmp) {
		if (*tmp == '-') {
			++tmp;
			tmp += strcspn(tmp, " ");
			tmp += strspn(tmp, " ");
		}
		char tube_name[255];
		uint64_t tube_name_len = strcspn(tmp, "\r\n");
		strlcpy(tube_name, tmp, tube_name_len + 1);
		tmp += tube_name_len;
		tmp += strspn(tmp, "\r\n");

		carglog(carg, L_INFO, "{\"fd\": %d, \"conn\": \"%s\", \"action\": \"beanstalkd tubes list handler\", \"tube_name\": \"%s\"}\n", carg->fd, carg->key, tube_name);

		char *query = malloc(255);
		snprintf(query, 254, "stats-tube %s\r\n", tube_name);
		char *key = malloc(255);
		snprintf(key, 254, "(tcp://%s:%s)/beanstalkd_tubes_%s", carg->host, carg->port, tube_name);
		try_again(carg, query, strlen(query), beanstalkd_stats_tube, "beanstalkd_stats_tube", NULL, key, carg->data);
	}
}

int8_t beanstalkd_validator(context_arg *carg, char *data, size_t size)
{
	if (strncmp(data, "OK", 2))
	{
		return 0;
	}

	uint64_t nsize = strtoull(data+3, NULL, 10);

	if (size >= nsize)
		return 1;
	else
		return 0;
}

string* beanstalkd_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add_auto(strdup("stats\r\n"));
}

string* beanstalkd_tubes_list_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add_auto(strdup("list-tubes\r\n"));
}

void beanstalkd_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("beanstalkd");
	actx->handlers = 2;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = beanstalkd_handler;
	actx->handler[0].validator = beanstalkd_validator;
	actx->handler[0].mesg_func = beanstalkd_mesg;
	strlcpy(actx->handler[0].key, "beanstalkd", 255);

	actx->handler[1].name = beanstalkd_tubes_list_handler;
	actx->handler[1].validator = NULL;
	actx->handler[1].mesg_func = beanstalkd_tubes_list_mesg;
	strlcpy(actx->handler[1].key, "beanstalkd_tubes_list", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
