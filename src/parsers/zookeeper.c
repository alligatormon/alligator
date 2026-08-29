#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/selector.h"
#include "common/validator.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "metric/labels.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"

#define ZK_NAME_SIZE 255
#define ZK_LABEL_SIZE 255
#define ZK_MAX_LABELS 16

static inline void zookeeper_metric_set(context_arg *carg, const char *metric_name)
{
	namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_GAUGE, "ZooKeeper exported metric value.");
}

static void zookeeper_skip_ws(const char **p, const char *end)
{
	while (*p < end && (**p == ' ' || **p == '\t'))
		++*p;
}

static alligator_ht *zookeeper_parse_prom_labels(const char *start, const char *end)
{
	alligator_ht *lbl = alligator_ht_init(NULL);
	const char *p = start;
	int nlabels = 0;

	while (p < end && nlabels < ZK_MAX_LABELS)
	{
		while (p < end && (*p == ' ' || *p == '\t' || *p == ','))
			++p;
		if (p >= end)
			break;

		const char *nstart = p;
		while (p < end && *p != '=' && *p != ' ' && *p != '\t' && *p != ',')
			++p;
		size_t nlen = (size_t)(p - nstart);
		if (!nlen)
			break;

		zookeeper_skip_ws(&p, end);
		if (p >= end || *p != '=')
			break;
		++p;
		zookeeper_skip_ws(&p, end);

		char quote = 0;
		if (p < end && (*p == '"' || *p == '\''))
		{
			quote = *p;
			++p;
		}

		const char *vstart = p;
		if (quote)
		{
			while (p < end && *p != quote)
				++p;
		}
		else
		{
			while (p < end && *p != ',' && *p != ' ' && *p != '\t')
				++p;
		}
		size_t vlen = (size_t)(p - vstart);
		if (quote && p < end && *p == quote)
			++p;

		char lname[ZK_LABEL_SIZE];
		char lval[ZK_LABEL_SIZE];
		size_t nc = nlen < (ZK_LABEL_SIZE - 1) ? nlen : (ZK_LABEL_SIZE - 1);
		size_t vc = vlen < (ZK_LABEL_SIZE - 1) ? vlen : (ZK_LABEL_SIZE - 1);
		memcpy(lname, nstart, nc);
		lname[nc] = 0;
		memcpy(lval, vstart, vc);
		lval[vc] = 0;

		prometheus_metric_name_normalizer(lname, nc);
		metric_label_value_validator_normalizer(lval, vc);
		if (lname[0])
			labels_hash_insert_nocache(lbl, lname, lval);
		++nlabels;
	}

	return lbl;
}

static void zookeeper_emit_numeric(context_arg *carg, char *name, alligator_ht *lbl, const char *valstr, size_t vlen)
{
	int rc = metric_value_validator((char *)valstr, vlen);
	if (!rc)
	{
		labels_hash_free(lbl);
		return;
	}

	zookeeper_metric_set(carg, name);

	if (rc == DATATYPE_INT)
	{
		int64_t val = strtoll(valstr, NULL, 10);
		metric_add(name, lbl, &val, rc, carg);
	}
	else if (rc == DATATYPE_UINT)
	{
		uint64_t val = strtoull(valstr, NULL, 10);
		metric_add(name, lbl, &val, rc, carg);
	}
	else if (rc == DATATYPE_DOUBLE)
	{
		double val = strtod(valstr, NULL);
		metric_add(name, lbl, &val, rc, carg);
	}
	else
		labels_hash_free(lbl);
}

void zookeeper_mntr_handler(char *metrics, size_t size, context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "zk_mode", METRIC_TYPE_GAUGE, "ZooKeeper node mode state.");

	if (!metrics || !size)
		return;

	const char *p = metrics;
	const char *end = metrics + size;

	while (p < end)
	{
		while (p < end && (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t'))
			++p;
		if (p >= end)
			break;

		const char *line_end = p;
		while (line_end < end && *line_end != '\n' && *line_end != '\r')
			++line_end;

		if (*p == '#')
		{
			p = line_end;
			continue;
		}

		const char *brace = NULL;
		const char *q = p;
		while (q < line_end)
		{
			if (*q == '{')
			{
				brace = q;
				break;
			}
			if (*q == ' ' || *q == '\t')
				break;
			++q;
		}

		char name[ZK_NAME_SIZE];
		alligator_ht *lbl = NULL;
		const char *valp;

		if (brace)
		{
			size_t nlen = (size_t)(brace - p);
			size_t nc = nlen < (ZK_NAME_SIZE - 1) ? nlen : (ZK_NAME_SIZE - 1);
			memcpy(name, p, nc);
			name[nc] = 0;

			const char *close = brace + 1;
			while (close < line_end && *close != '}')
				++close;
			if (close >= line_end)
			{
				p = line_end;
				continue;
			}

			lbl = zookeeper_parse_prom_labels(brace + 1, close);
			valp = close + 1;
			zookeeper_skip_ws(&valp, line_end);
		}
		else
		{
			size_t nlen = (size_t)(q - p);
			size_t nc = nlen < (ZK_NAME_SIZE - 1) ? nlen : (ZK_NAME_SIZE - 1);
			memcpy(name, p, nc);
			name[nc] = 0;
			valp = q;
			zookeeper_skip_ws(&valp, line_end);
		}

		prometheus_metric_name_normalizer(name, strlen(name));

		if (!strcmp(name, "zk_server_state"))
		{
			labels_hash_free(lbl);
			int64_t val = 1;
			if (strstr(valp, "standalone") == valp)
				metric_add_labels("zk_mode", &val, DATATYPE_INT, carg, "mode", "standalone");
			else if (strstr(valp, "follower") == valp)
				metric_add_labels("zk_mode", &val, DATATYPE_INT, carg, "mode", "follower");
			else if (strstr(valp, "leader") == valp)
				metric_add_labels("zk_mode", &val, DATATYPE_INT, carg, "mode", "leader");
			carg->parser_status = 1;
			p = line_end;
			continue;
		}

		if (valp >= line_end)
		{
			labels_hash_free(lbl);
			p = line_end;
			continue;
		}

		size_t vlen = (size_t)(line_end - valp);
		while (vlen && (valp[vlen - 1] == ' ' || valp[vlen - 1] == '\t'))
			--vlen;

		zookeeper_emit_numeric(carg, name, lbl, valp, vlen);
		p = line_end;
	}
}
void zookeeper_wchs_handler(char *metrics, size_t size, context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "zk_total_watches", METRIC_TYPE_GAUGE, "ZooKeeper total watches.");

	char *cur = strstr(metrics, "Total watches:");
	if (!cur)
		return;
	int64_t pvalue = strtoll(cur + 14, NULL, 10);
	metric_add_auto("zk_total_watches", &pvalue, DATATYPE_INT, carg);
	carg->parser_status = 1;
}
void zookeeper_isro_handler(char *metrics, size_t size, context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "zk_readwrite", METRIC_TYPE_GAUGE, "ZooKeeper read/write mode state.");

	int64_t val = 1;
	if (!strncmp(metrics, "ro", 2))
	{
		metric_add_labels("zk_readwrite", &val, DATATYPE_INT, carg, "status", "ro");
		carg->parser_status = 0;
	}
	else if (!strncmp(metrics, "rw", 2))
	{
		metric_add_labels("zk_readwrite", &val, DATATYPE_INT, carg, "status", "rw");
		carg->parser_status = 1;
	}
	else if (!strncmp(metrics, "null", 4))
	{
		metric_add_labels("zk_readwrite", &val, DATATYPE_INT, carg, "status", "null");
		carg->parser_status = 0;
	}
}

string* zookeeper_mntr_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("mntr", 4);
}

string* zookeeper_isro_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("isro", 4);
}

string* zookeeper_wchs_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_alloc("wchs", 4);
}

void zookeeper_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("zookeeper");
	actx->handlers = 3;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = zookeeper_mntr_handler;
	//actx->handler[0].validator = zookeeper_validator;
	actx->handler[0].mesg_func = zookeeper_mntr_mesg;
	strlcpy(actx->handler[0].key,"zookeeper_mntr", 255);

	actx->handler[1].name = zookeeper_isro_handler;
	//actx->handler[1].validator = zookeeper_validator;
	actx->handler[1].mesg_func = zookeeper_isro_mesg;
	strlcpy(actx->handler[1].key,"zookeeper_isro", 255);

	actx->handler[2].name = zookeeper_wchs_handler;
	//actx->handler[2].validator = zookeeper_validator;
	actx->handler[2].mesg_func = zookeeper_wchs_mesg;
	strlcpy(actx->handler[2].key,"zookeeper_wchs", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
