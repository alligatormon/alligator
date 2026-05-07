#include "common/selector.h"
#include "parsers/multiparser.h"
#include "events/fs_read.h"
#include "main.h"
#include <ctype.h>

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

static void metric_dump_node(metric_node *x, const char *ns_name, string *out)
{
	if (!x)
		return;

	metric_dump_node(x->child[LEFT], ns_name, out);

	if (x->labels && x->expire_node)
	{
		const char *metric_name = metric_dump_name(x->labels);
		if (metric_name)
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
			else if (x->type == DATATYPE_STRING)
				json_object_set_new_nocheck(row, "value", json_string(x->s ? x->s : ""));
			else if (x->type == DATATYPE_LIST_INT || x->type == DATATYPE_LIST_UINT || x->type == DATATYPE_LIST_DOUBLE || x->type == DATATYPE_LIST_STRING)
			{
				json_t *arr = json_array();
				uint64_t list_sz = x->list_len < METRIC_STORAGE_BUFFER_DEFAULT ? x->list_len : METRIC_STORAGE_BUFFER_DEFAULT;
				for (uint64_t i = 0; x->list && i < list_sz; ++i)
				{
					if (x->type == DATATYPE_LIST_INT)
						json_array_append_new(arr, json_integer(x->list[i].i));
					else if (x->type == DATATYPE_LIST_UINT)
						json_array_append_new(arr, json_integer((json_int_t)x->list[i].u));
					else if (x->type == DATATYPE_LIST_DOUBLE)
						json_array_append_new(arr, json_real(x->list[i].d));
					else if (x->type == DATATYPE_LIST_STRING)
						json_array_append_new(arr, json_string(x->list[i].s ? x->list[i].s : ""));
				}
				json_object_set_new_nocheck(row, "values", arr);
			}

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

	metric_dump_node(x->child[RIGHT], ns_name, out);
}

static void metric_dump_namespace(void *funcarg, void *arg)
{
	string *out = funcarg;
	namespace_struct *ns = arg;
	if (!out || !ns || !ns->metrictree || !ns->metrictree->rwlock)
		return;

	pthread_rwlock_rdlock(ns->metrictree->rwlock);
	if (ns->metrictree->root)
		metric_dump_node(ns->metrictree->root, ns->key, out);
	pthread_rwlock_unlock(ns->metrictree->rwlock);
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

	if (type == DATATYPE_INT || type == DATATYPE_UINT || type == DATATYPE_DOUBLE || type == DATATYPE_STRING)
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
		else if (type == DATATYPE_STRING) {
			char *v = (char*)json_string_value(jvalue);
			metric_add((char*)json_string_value(jname), labels, &v, type, &carg);
		}
		return;
	}

	if (type == DATATYPE_LIST_INT || type == DATATYPE_LIST_UINT || type == DATATYPE_LIST_DOUBLE || type == DATATYPE_LIST_STRING)
	{
		json_t *jvalues = json_object_get(row, "values");
		if (!json_is_array(jvalues))
			return;
		size_t i;
		json_t *jv;
		json_array_foreach(jvalues, i, jv)
		{
			alligator_ht *labels = metric_restore_labels(labels_obj);
			if (type == DATATYPE_LIST_INT) {
				int64_t v = json_integer_value(jv);
				metric_add((char*)json_string_value(jname), labels, &v, type, &carg);
			}
			else if (type == DATATYPE_LIST_UINT) {
				uint64_t v = (uint64_t)json_integer_value(jv);
				metric_add((char*)json_string_value(jname), labels, &v, type, &carg);
			}
			else if (type == DATATYPE_LIST_DOUBLE) {
				double v = json_number_value(jv);
				metric_add((char*)json_string_value(jname), labels, &v, type, &carg);
			}
			else if (type == DATATYPE_LIST_STRING) {
				char *v = (char*)json_string_value(jv);
				metric_add((char*)json_string_value(jname), labels, &v, type, &carg);
			}
		}
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
	read_from_file(strdup(dirtoread), 0, restore_callback, NULL);
}
