#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "metric/namespace.h"
#include "common/validator.h"
#include <stdlib.h>
#include <pthread.h>
#include "metric/namespace.h"
#include "metric/labels.h"
#include "metric/metrictree.h"
#include "main.h"
#include "parsers/pushgateway.h"
#define METRIC_LABEL_MAX_SIZE 255

static int pushgateway_char_equal(char a, char b)
{
	return tolower((unsigned char)a) == tolower((unsigned char)b);
}

static const char *pushgateway_memcasestr(const char *hay, size_t hay_len, const char *needle)
{
	size_t needle_len = strlen(needle);
	if (!needle_len || hay_len < needle_len)
		return NULL;

	size_t last = hay_len - needle_len;
	for (size_t i = 0; i <= last; ++i)
	{
		if (i && hay[i - 1] != '\n')
			continue;

		size_t j = 0;
		for (; j < needle_len; ++j)
			if (!pushgateway_char_equal(hay[i + j], needle[j]))
				break;
		if (j == needle_len)
			return hay + i;
	}
	return NULL;
}

static int pushgateway_token_equal(const char *s, size_t len, const char *cmp)
{
	size_t cmp_len = strlen(cmp);
	if (len != cmp_len)
		return 0;
	for (size_t i = 0; i < len; ++i)
		if (!pushgateway_char_equal(s[i], cmp[i]))
			return 0;
	return 1;
}

static void pushgateway_param_trim(const char **s, size_t *len)
{
	while (*len && (**s == ' ' || **s == '\t'))
	{
		++*s;
		--*len;
	}
	while (*len && ((*s)[*len - 1] == ' ' || (*s)[*len - 1] == '\t'))
		--*len;
}

static void pushgateway_param_unquote(const char **s, size_t *len)
{
	if (*len >= 2 && (*s)[0] == '"' && (*s)[*len - 1] == '"')
	{
		++*s;
		*len -= 2;
	}
}

pg_body_format pushgateway_body_format(const char *headers, size_t headers_size)
{
	if (!headers || !headers_size)
		return PG_BODY_TEXT;

	const char *ct = pushgateway_memcasestr(headers, headers_size, "content-type:");
	if (!ct)
		return PG_BODY_TEXT;

	const char *value = ct + strlen("content-type:");
	const char *value_start = value;
	while (value_start < headers + headers_size && (*value_start == ' ' || *value_start == '\t'))
		++value_start;

	const char *value_end = value_start;
	while (value_end < headers + headers_size && *value_end != '\r' && *value_end != '\n')
		++value_end;

	size_t value_len = value_end - value_start;
	if (!value_len)
		return PG_BODY_TEXT;

	const char *semi = memchr(value_start, ';', value_len);
	size_t media_len = semi ? (size_t)(semi - value_start) : value_len;
	const char *media = value_start;
	pushgateway_param_trim(&media, &media_len);

	int media_protobuf = pushgateway_token_equal(media, media_len, "application/vnd.google.protobuf") ||
		pushgateway_token_equal(media, media_len, "application/x-protobuf");
	if (!media_protobuf)
		return PG_BODY_TEXT;

	int proto_seen = 0;
	int proto_ok = 0;
	int encoding_seen = 0;
	pg_body_format encoding = PG_BODY_PROTO_DELIM;

	const char *p = semi ? semi + 1 : value_end;
	const char *line_end = value_end;
	while (p < line_end)
	{
		const char *seg = p;
		const char *seg_end = memchr(p, ';', line_end - p);
		if (!seg_end)
			seg_end = line_end;
		p = seg_end < line_end ? seg_end + 1 : line_end;

		size_t seg_len = seg_end - seg;
		pushgateway_param_trim(&seg, &seg_len);
		if (!seg_len)
			continue;

		const char *eq = memchr(seg, '=', seg_len);
		if (!eq)
			continue;

		const char *key = seg;
		size_t key_len = eq - seg;
		pushgateway_param_trim(&key, &key_len);

		const char *val = eq + 1;
		size_t val_len = seg_end - val;
		pushgateway_param_trim(&val, &val_len);
		pushgateway_param_unquote(&val, &val_len);

		if (pushgateway_token_equal(key, key_len, "proto"))
		{
			proto_seen = 1;
			proto_ok = pushgateway_token_equal(val, val_len, "io.prometheus.client.MetricFamily");
		}
		else if (pushgateway_token_equal(key, key_len, "encoding"))
		{
			encoding_seen = 1;
			if (pushgateway_token_equal(val, val_len, "delimited"))
				encoding = PG_BODY_PROTO_DELIM;
			else
				encoding = PG_BODY_PROTO_UNSUPPORTED;
		}
	}

	if (proto_seen && !proto_ok)
		return PG_BODY_TEXT;

	if (encoding_seen)
		return encoding;

	return PG_BODY_PROTO_DELIM;
}

alligator_ht *get_labels_from_url_pushgateway_format(char *uri, size_t uri_size, context_arg *carg)
{
	if (!uri)
		return 0;

	if (!uri_size)
		return 0;

	char *metrics_str  = strstr(uri, "/metrics/");
	if (!metrics_str)
		return 0;

	alligator_ht *lbl = alligator_ht_init(NULL);
	uint64_t index_get, prev_get;
	char key[METRIC_LABEL_MAX_SIZE];
	char mname[METRIC_LABEL_MAX_SIZE];

	for (index_get = metrics_str - uri + 9; index_get < uri_size;)
	{
		for (; index_get < uri_size && uri[index_get]=='/' && uri[index_get]!=0; ++index_get);
		prev_get = index_get;
		for (; index_get < uri_size && uri[index_get]!='/' && uri[index_get]!=0; ++index_get);
		size_t mname_len = uint_min(index_get-prev_get+1, METRIC_LABEL_MAX_SIZE);
		strlcpy(mname, uri+prev_get, mname_len);

		//prev_get = index_get;
		for (; index_get < uri_size && uri[index_get]=='/' && uri[index_get]!=0; ++index_get);
		if (index_get >= uri_size || uri[index_get] == 0)
			break;

		prev_get = index_get;
		for (; index_get < uri_size && uri[index_get]!='/' && uri[index_get]!=0; ++index_get);
		size_t key_len = uint_min(index_get-prev_get+1, METRIC_LABEL_MAX_SIZE);
		strlcpy(key, uri+prev_get, key_len);

		metric_label_value_validator_normalizer(key, key_len);


		metric_name_normalizer(mname, mname_len);
		labels_hash_insert_nocache(lbl, mname, key);

		++index_get;
		if (index_get >= uri_size || uri[index_get]==0)
			break;
	}

	carg->parser_status = 1;
	return lbl;
}

typedef struct pg_delete_entry
{
	char *name;
	alligator_ht *labels;
} pg_delete_entry;

typedef struct pg_delete_list
{
	pg_delete_entry *items;
	size_t size;
	size_t cap;
	int oom;
} pg_delete_list;

static void pg_delete_collect_node(metric_node *x, sortplan *sort_plan, labels_t *query, size_t labels_count, pg_delete_list *list)
{
	if (!x)
		return;

	pg_delete_collect_node(x->child[LEFT], sort_plan, query, labels_count, list);
	pg_delete_collect_node(x->child[RIGHT], sort_plan, query, labels_count, list);

	if (list->oom || !x->labels || !x->labels->key)
		return;
	if (labels_match(sort_plan, x->labels, query, labels_count))
		return;

	if (list->size == list->cap)
	{
		size_t ncap = list->cap ? list->cap * 2 : 16;
		pg_delete_entry *nitems = realloc(list->items, ncap * sizeof(*nitems));
		if (!nitems)
		{
			list->oom = 1;
			return;
		}
		list->items = nitems;
		list->cap = ncap;
	}

	pg_delete_entry *e = &list->items[list->size];
	e->name = strdup(x->labels->key);
	e->labels = alligator_ht_init(NULL);
	if (!e->name || !e->labels)
	{
		free(e->name);
		if (e->labels)
		{
			alligator_ht_done(e->labels);
			free(e->labels);
		}
		list->oom = 1;
		return;
	}

	for (labels_t *l = x->labels->next; l; l = l->next)
		if (l->name && l->key)
			labels_hash_insert_nocache(e->labels, l->name, l->key);
	++list->size;
}

int check_collisions_compare(const void* arg, const void* obj);

typedef struct pg_plan_probe
{
	sortplan *sort_plan;
	int missing;
} pg_plan_probe;

/* 1 when the URL label name is already in the sort plan; a series can only carry such labels */
static void pg_plan_probe_for(void *funcarg, void *arg)
{
	pg_plan_probe *probe = funcarg;
	labels_container *lc = arg;
	if (probe->missing || !lc || !lc->name)
		return;
	uint32_t h = ac->metrictree_hashfunc_get(lc->name);
	if (!alligator_ht_search(probe->sort_plan->check_collisions, check_collisions_compare, lc->name, h))
		probe->missing = 1;
}

/*
 * Odd number of non-empty path components after /metrics/ (`/metrics/job/a/instance`):
 * the URL label parser silently drops the dangling name, which would widen a DELETE.
 * Upstream Pushgateway answers 400 for it.
 */
static int pg_url_components_odd(const char *uri, size_t uri_size)
{
	const char *m = strstr(uri, "/metrics/");
	if (!m)
		return 0;

	size_t i = (size_t)(m - uri) + strlen("/metrics/");
	size_t n = 0;
	while (i < uri_size && uri[i])
	{
		while (i < uri_size && uri[i] == '/')
			++i;
		if (i >= uri_size || !uri[i])
			break;
		++n;
		while (i < uri_size && uri[i] && uri[i] != '/')
			++i;
	}
	return n & 1;
}

int pushgateway_delete(const char *uri, size_t uri_size, context_arg *carg)
{
	if (!uri || !uri_size || !carg)
		return 404;

	/* the URL parser sets parser_status as a push side effect, keep it untouched */
	int saved_parser_status = carg->parser_status;
	alligator_ht *url_labels = get_labels_from_url_pushgateway_format((char *)uri, uri_size, carg);
	carg->parser_status = saved_parser_status;
	if (!url_labels)
		return 404;

	if (!alligator_ht_search(url_labels, labels_hash_compare, "job", ac->metrictree_hashfunc_get("job")) ||
		pg_url_components_odd(uri, uri_size))
	{
		labels_hash_free(url_labels);
		return 400;
	}

	namespace_struct *ns = get_namespace_by_carg(carg);
	if (!ns || !ns->metrictree)
	{
		labels_hash_free(url_labels);
		return 202;
	}
	metric_tree *tree = ns->metrictree;

	/* A label name outside the sort plan cannot be on any stored series. Return before
	 * labels_initiate() would append it to the shared, append-only plan (URL-driven growth). */
	pg_plan_probe probe = { tree->sort_plan, 0 };
	alligator_ht_foreach_arg(url_labels, pg_plan_probe_for, &probe);
	if (probe.missing)
	{
		labels_hash_free(url_labels);
		return 202;
	}

	size_t labels_count = alligator_ht_count(url_labels);
	/* NULL name => key_len == 0 => any metric name matches (see labels_match) */
	alligator_ht *query_hash = labels_dup(url_labels);
	labels_hash_free(url_labels);
	labels_t *query = labels_initiate(ns, query_hash, NULL, NULL, ns, 0);
	if (!query)
	{
		/* labels_initiate leaves the hash with the caller when it fails to allocate the chain */
		labels_hash_free(query_hash);
		return 500;
	}

	pg_delete_list list = {0};
	pthread_rwlock_rdlock(tree->rwlock);
	pg_delete_collect_node(tree->root, tree->sort_plan, query, labels_count, &list);
	pthread_rwlock_unlock(tree->rwlock);
	labels_head_free(query);

	int status = list.oom ? 500 : 202;
	for (size_t i = 0; i < list.size; ++i)
	{
		labels_t *ll = labels_initiate(ns, list.items[i].labels, list.items[i].name, NULL, ns, 0);
		if (ll)
		{
			metric_delete(tree, ll, ns->expiretree);
			labels_head_free(ll);
		}
		else
		{
			labels_hash_free(list.items[i].labels);
			status = 500;
		}
		free(list.items[i].name);
	}
	free(list.items);

	return status;
}
