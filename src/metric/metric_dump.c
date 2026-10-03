#include "common/selector.h"
#include "parsers/multiparser.h"
#include "events/fs_read.h"
#include "metric/metric_dump.h"
#include "metric/namespace.h"
#include "metric/labels.h"
#include "query/promql.h"
#include "common/logs.h"
#include "main.h"
#include <ctype.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct metric_dump_selector {
	labels_t *labels;
	size_t labels_count;
	metric_query_context *mqc;
};

struct metric_dump_filter {
	int pass_all;
	int fail_closed;
	sortplan *plan;
	struct metric_dump_selector *sel;
	size_t n;
};

/* Held across persistence_promql_apply / free and the dump snapshot.
   The walk itself uses the filter's own compiled copy. */
static pthread_mutex_t persistence_promql_mu = PTHREAD_MUTEX_INITIALIZER;

void persistence_promql_lock(void)
{
	pthread_mutex_lock(&persistence_promql_mu);
}

void persistence_promql_unlock(void)
{
	pthread_mutex_unlock(&persistence_promql_mu);
}

static int persistence_selector_usable(metric_query_context *mqc)
{
	if (!mqc)
		return 0;
	if (mqc->name_re_invalid || mqc->selector_partial)
		return 0;
	if (mqc->func != QUERY_FUNC_NONE)
		return 0;
	if (mqc->op != QUERY_OPERATOR_NOOP)
		return 0;
	if (mqc->name && mqc->name[0])
		return 1;
	if (mqc->name_re)
		return 1;
	if (mqc->lbl && alligator_ht_count(mqc->lbl) > 0)
		return 1;
	return 0;
}

static void persistence_promql_clear(void)
{
	size_t i;

	if (!ac)
		return;
	if (ac->persistence_promql)
	{
		for (i = 0; i < ac->persistence_promql_count; i++)
			free(ac->persistence_promql[i]);
		free(ac->persistence_promql);
	}
	if (ac->persistence_promql_mqc)
	{
		for (i = 0; i < ac->persistence_promql_count; i++)
			query_context_free(ac->persistence_promql_mqc[i]);
		free(ac->persistence_promql_mqc);
	}
	ac->persistence_promql = NULL;
	ac->persistence_promql_mqc = NULL;
	ac->persistence_promql_count = 0;
}

void persistence_promql_free(void)
{
	if (!ac)
		return;
	persistence_promql_lock();
	persistence_promql_clear();
	persistence_promql_unlock();
}

void persistence_promql_apply(json_t *promql)
{
	size_t n;
	size_t i;

	if (!ac)
		return;
	persistence_promql_lock();
	persistence_promql_clear();
	if (!promql)
		goto out;

	if (!json_is_array(promql))
	{
		glog(L_ERROR, "persistence: promql must be an array of selector strings\n");
		ac->persistence_promql_count = 1;
		goto out;
	}

	n = json_array_size(promql);
	if (!n)
		goto out;

	ac->persistence_promql = calloc(n, sizeof(char *));
	ac->persistence_promql_mqc = calloc(n, sizeof(metric_query_context *));
	if (!ac->persistence_promql || !ac->persistence_promql_mqc)
	{
		free(ac->persistence_promql);
		free(ac->persistence_promql_mqc);
		ac->persistence_promql = NULL;
		ac->persistence_promql_mqc = NULL;
		ac->persistence_promql_count = n;
		glog(L_ERROR, "persistence: out of memory compiling promql selectors\n");
		goto out;
	}

	for (i = 0; i < n; i++)
	{
		json_t *el = json_array_get(promql, i);
		const char *expr;
		metric_query_context *mqc;

		if (!json_is_string(el))
		{
			glog(L_ERROR, "persistence: promql[%zu] is not a string\n", i);
			ac->persistence_promql[i] = strdup("");
			continue;
		}

		expr = json_string_value(el);
		ac->persistence_promql[i] = strdup(expr ? expr : "");
		mqc = promql_parser(NULL, (char *)(expr ? expr : ""), expr ? strlen(expr) : 0);
		if (mqc)
			mqc->query = NULL;
		if (!persistence_selector_usable(mqc))
		{
			glog(L_ERROR, "persistence: skip invalid promql selector '%s'\n", expr ? expr : "");
			query_context_free(mqc);
			continue;
		}
		ac->persistence_promql_mqc[i] = mqc;
	}
	ac->persistence_promql_count = n;
out:
	persistence_promql_unlock();
}

static metric_query_context *persistence_selector_compile_owned(char *expr)
{
	metric_query_context *mqc;

	if (!expr)
		return NULL;
	mqc = promql_parser(NULL, expr, strlen(expr));
	if (mqc)
		mqc->query = NULL;
	if (!persistence_selector_usable(mqc))
	{
		glog(L_ERROR, "persistence: skip invalid promql selector '%s'\n", expr);
		query_context_free(mqc);
		return NULL;
	}
	return mqc;
}

metric_dump_filter *metric_dump_filter_build(namespace_struct *ns)
{
	metric_dump_filter *f = calloc(1, sizeof(*f));
	char **snap = NULL;
	size_t snap_n = 0;
	size_t count = 0;
	size_t i;

	if (!f)
		return NULL;
	f->plan = (ns && ns->metrictree) ? ns->metrictree->sort_plan : NULL;
	if (!ac)
	{
		f->pass_all = 1;
		return f;
	}

	/* Copy selector text under the lock, then compile that copy. Replacing
	   or deleting persistence mid-dump frees only the config-owned list. */
	persistence_promql_lock();
	count = ac->persistence_promql_count;
	if (!count)
	{
		persistence_promql_unlock();
		f->pass_all = 1;
		return f;
	}
	if (!f->plan || !ns || !ac->persistence_promql || !ac->persistence_promql_mqc)
	{
		persistence_promql_unlock();
		f->fail_closed = 1;
		return f;
	}
	snap = calloc(count, sizeof(char *));
	if (!snap)
	{
		persistence_promql_unlock();
		f->fail_closed = 1;
		return f;
	}
	for (i = 0; i < count; i++)
	{
		if (!ac->persistence_promql_mqc[i] || !ac->persistence_promql[i])
			continue;
		snap[snap_n] = strdup(ac->persistence_promql[i]);
		if (!snap[snap_n])
			continue;
		snap_n++;
	}
	persistence_promql_unlock();

	if (!snap_n)
	{
		free(snap);
		f->fail_closed = 1;
		return f;
	}

	f->sel = calloc(snap_n, sizeof(*f->sel));
	if (!f->sel)
	{
		for (i = 0; i < snap_n; i++)
			free(snap[i]);
		free(snap);
		f->fail_closed = 1;
		return f;
	}

	for (i = 0; i < snap_n; i++)
	{
		metric_query_context *mqc = persistence_selector_compile_owned(snap[i]);
		alligator_ht *hash_work;
		size_t labels_count;
		labels_t *labels_list;

		free(snap[i]);
		if (!mqc)
			continue;
		hash_work = mqc->lbl ? labels_dup(mqc->lbl) : alligator_ht_init(NULL);
		if (!hash_work)
		{
			query_context_free(mqc);
			continue;
		}
		labels_count = alligator_ht_count(hash_work);
		labels_list = labels_initiate(ns, hash_work, mqc->name, 0, 0, 0);
		if (!labels_list)
		{
			labels_hash_free(hash_work);
			query_context_free(mqc);
			continue;
		}
		f->sel[f->n].labels = labels_list;
		f->sel[f->n].labels_count = labels_count;
		f->sel[f->n].mqc = mqc;
		f->n++;
	}
	free(snap);
	if (!f->n)
		f->fail_closed = 1;
	return f;
}

void metric_dump_filter_free(metric_dump_filter *filter)
{
	size_t i;

	if (!filter)
		return;
	if (filter->sel)
	{
		for (i = 0; i < filter->n; i++)
		{
			labels_head_free(filter->sel[i].labels);
			/* Owns the compiled copy, not ac->persistence_promql_mqc. */
			query_context_free(filter->sel[i].mqc);
		}
		free(filter->sel);
	}
	free(filter);
}

int metric_dump_filter_aliases_config(metric_dump_filter *filter)
{
	size_t i;
	size_t j;
	int alias = 0;

	if (!filter || !filter->sel || !filter->n || !ac)
		return 0;

	persistence_promql_lock();
	if (ac->persistence_promql_mqc && ac->persistence_promql_count)
	{
		for (i = 0; i < filter->n && !alias; i++)
		{
			metric_query_context *mine = filter->sel[i].mqc;
			const char *name = (filter->sel[i].labels && filter->sel[i].labels->key) ? filter->sel[i].labels->key : NULL;

			for (j = 0; j < ac->persistence_promql_count; j++)
			{
				metric_query_context *cfg = ac->persistence_promql_mqc[j];
				if (!cfg)
					continue;
				if (mine == cfg)
					alias = 1;
				else if (name && cfg->name && name == cfg->name)
					alias = 1;
				else if (mine && mine->name && cfg->name && mine->name == cfg->name)
					alias = 1;
				if (alias)
					break;
			}
		}
	}
	persistence_promql_unlock();
	return alias;
}

int metric_dump_series_selected(metric_dump_filter *filter, labels_t *series)
{
	size_t i;

	if (!series)
		return 0;
	if (!filter)
		return (ac && ac->persistence_promql_count) ? 0 : 1;
	if (filter->pass_all)
		return 1;
	if (filter->fail_closed || !filter->n || !filter->plan)
		return 0;
	for (i = 0; i < filter->n; i++)
	{
		if (metric_selector_match(filter->plan, series, filter->sel[i].labels, filter->sel[i].labels_count, filter->sel[i].mqc))
			return 1;
	}
	return 0;
}

//void metric_dump_free(char *data)
//{
//	string_free(data);
//}

static const char* metric_dump_name(labels_t *labels)
{
	for (labels_t *cur = labels; cur; cur = cur->next)
	{
		if (cur->name && cur->key && !strcmp(cur->name, MAIN_METRIC_NAME))
			return cur->key;
	}
	return NULL;
}

static json_t* metric_dump_labels(labels_t *labels)
{
	json_t *obj = json_object();
	for (labels_t *cur = labels; cur; cur = cur->next)
	{
		if (!cur->name || !cur->key)
			continue;
		if (!strcmp(cur->name, MAIN_METRIC_NAME))
			continue;
		json_object_set_new_nocheck(obj, cur->name, json_string(cur->key));
	}
	return obj;
}

static void metric_dump_node(metric_node *x, const char *ns_name, string *out, metric_dump_filter *filter)
{
	if (!x)
		return;

	metric_dump_node(x->child[LEFT], ns_name, out, filter);

	if (x->labels && x->expire_node)
	{
		const char *metric_name = metric_dump_name(x->labels);
		if (metric_name && metric_dump_series_selected(filter, x->labels))
		{
			json_t *row = json_object();
			json_object_set_new_nocheck(row, "namespace", json_string(ns_name ? ns_name : "default"));
			json_object_set_new_nocheck(row, "name", json_string(metric_name));
			json_object_set_new_nocheck(row, "type", json_integer(x->type));
			json_object_set_new_nocheck(row, "expire", json_integer(x->expire_node->key));
			json_object_set_new_nocheck(row, "labels", metric_dump_labels(x->labels));

			if (x->type == DATATYPE_INT)
				json_object_set_new_nocheck(row, "value", json_integer(x->i));
			else if (x->type == DATATYPE_UINT)
				json_object_set_new_nocheck(row, "value", json_integer((json_int_t)x->u));
			else if (x->type == DATATYPE_DOUBLE)
				json_object_set_new_nocheck(row, "value", json_real(x->d));

			char *line = json_dumps(row, JSON_COMPACT);
			if (line)
			{
				string_cat(out, line, strlen(line));
				string_cat(out, "\n", 1);
				free(line);
			}
			json_decref(row);
		}
	}

	metric_dump_node(x->child[RIGHT], ns_name, out, filter);
}

static void metric_dump_namespace(void *funcarg, void *arg)
{
	string *out = funcarg;
	namespace_struct *ns = arg;
	if (!out || !ns || !ns->metrictree || !ns->metrictree->rwlock)
		return;

	metric_dump_filter *filter = metric_dump_filter_build(ns);
	pthread_rwlock_rdlock(ns->metrictree->rwlock);
	if (ns->metrictree->root)
		metric_dump_node(ns->metrictree->root, ns->key, out, filter);
	pthread_rwlock_unlock(ns->metrictree->rwlock);
	metric_dump_filter_free(filter);
}

void metric_dump(int exit_sig)
{
	extern aconf *ac;
	if (!ac->persistence_dir)
		return;

	string *body = string_init(10000000);
	alligator_ht_foreach_arg(ac->_namespace, metric_dump_namespace, body);
	char dirtowrite[255];
	snprintf(dirtowrite, 255, "%s/metric_dump", ac->persistence_dir);
	if (exit_sig >= 0)
	{
		FILE *fd = fopen(dirtowrite, "w");
		if (!fd)
		{
			string_free(body);
			return;
		}
		fwrite(body->s, body->l, 1, fd);
		fclose(fd);
		string_free(body);
	}
	else
		write_to_file(dirtowrite, body->s, body->l, free, body);
}

static alligator_ht* metric_restore_labels(json_t *labels)
{
	alligator_ht *hash = alligator_ht_init(NULL);
	if (!labels || !json_is_object(labels))
		return hash;

	const char *k;
	json_t *v;
	json_object_foreach(labels, k, v)
	{
		if (!json_is_string(v))
			continue;
		labels_hash_insert_nocache(hash, (char*)k, (char*)json_string_value(v));
	}
	return hash;
}

static void metric_restore_row(json_t *row)
{
	if (!row || !json_is_object(row))
		return;

	json_t *jns = json_object_get(row, "namespace");
	json_t *jname = json_object_get(row, "name");
	json_t *jtype = json_object_get(row, "type");
	json_t *jexpire = json_object_get(row, "expire");
	if (!json_is_string(jname) || !json_is_integer(jtype) || !json_is_integer(jexpire))
		return;

	r_time now = setrtime();
	int64_t remaining_ttl = json_integer_value(jexpire) - now.sec;
	if (remaining_ttl <= 0)
		return;

	context_arg carg = {0};
	carg.namespace = json_is_string(jns) ? (char*)json_string_value(jns) : "default";
	carg.ttl = -1;
	carg.curr_ttl = remaining_ttl;

	int8_t type = (int8_t)json_integer_value(jtype);
	json_t *labels_obj = json_object_get(row, "labels");

	if (type == DATATYPE_INT || type == DATATYPE_UINT || type == DATATYPE_DOUBLE)
	{
		alligator_ht *labels = metric_restore_labels(labels_obj);
		json_t *jvalue = json_object_get(row, "value");
		if (!jvalue)
			return;

		if (type == DATATYPE_INT) {
			int64_t v = json_integer_value(jvalue);
			metric_add((char*)json_string_value(jname), labels, &v, type, &carg);
		}
		else if (type == DATATYPE_UINT) {
			uint64_t v = (uint64_t)json_integer_value(jvalue);
			metric_add((char*)json_string_value(jname), labels, &v, type, &carg);
		}
		else if (type == DATATYPE_DOUBLE) {
			double v = json_number_value(jvalue);
			metric_add((char*)json_string_value(jname), labels, &v, type, &carg);
		}
		return;
	}
}

void restore_callback(char *buf, size_t len, void *data)
{
	(void)data;
	size_t i = 0;
	while (i < len && isspace((unsigned char)buf[i]))
		++i;
	if (i >= len)
		return;

	/* Backward compatibility for old persistence files in Prometheus text format. */
	if (buf[i] != '{') {
		alligator_multiparser(buf, len, NULL, NULL, NULL);
		return;
	}

	size_t start = 0;
	for (size_t p = 0; p <= len; ++p)
	{
		if (p != len && buf[p] != '\n')
			continue;

		size_t line_start = start;
		size_t line_end = p;
		while (line_start < line_end && isspace((unsigned char)buf[line_start]))
			++line_start;
		while (line_end > line_start && isspace((unsigned char)buf[line_end - 1]))
			--line_end;

		if (line_end > line_start)
		{
			json_error_t err;
			json_t *row = json_loadb(buf + line_start, line_end - line_start, 0, &err);
			if (row)
			{
				metric_restore_row(row);
				json_decref(row);
			}
		}
		start = p + 1;
	}
}

void metric_restore()
{
	extern aconf *ac;

	char dirtoread[255];
	snprintf(dirtoread, 255, "%s/metric_dump", ac->persistence_dir);
	/* read_from_file stops at MAX_FILE_SIZE (1 MB) and drops the tail. */
	read_whole_file(strdup(dirtoread), restore_callback, NULL);
}
