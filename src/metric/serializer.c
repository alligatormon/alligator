#include "common/selector.h"
#include "metric/namespace.h"
#include "common/rtime.h"
#include "common/selector.h"
#include "common/json_query.h"
#include "common/validator.h"
#include <uuid/uuid.h>
#include <time.h>
#include <inttypes.h>
#include <stdlib.h>
#include <math.h>
#define SQL_CREATE "CREATE TABLE IF NOT EXISTS "
#define SQL_INSERT "INSERT INTO "
#define CLICKHOUSE_ENGINE "ENGINE=MergeTree ORDER BY timestamp"
#define CLICKHOUSE_STANDARD_FIELDS "(timestamp DateTime, value Float64"
#define CLICKHOUSE_INSERT_FIELDS "(timestamp, value"
#define PG_STANDARD_FIELDS "(ts tm, value double precision"
#define PG_INSERT_FIELDS "(ts, value"
#define CASSANDRA_STANDARD_FIELDS "(id UUID PRIMARY KEY, time TIMESTAMP, value DOUBLE"
#define CASSANDRA_INSERT_FIELDS "(time, id, value"

void otlp_protobuf_serialize(metric_node *x, serializer_context *sc);

static json_t *otlp_export_metrics_root_new(void)
{
	json_t *root = json_object();
	json_t *resourceMetrics = json_array();
	json_t *rm = json_object();
	json_t *resource = json_object();
	json_object_set_new(resource, "attributes", json_array());
	json_object_set_new(rm, "resource", resource);
	json_t *scopeMetrics = json_array();
	json_t *sm = json_object();
	json_object_set_new(sm, "scope", json_object());
	json_t *metrics = json_array();
	json_object_set_new(sm, "metrics", metrics);
	json_array_append_new(scopeMetrics, sm);
	json_object_set_new(rm, "scopeMetrics", scopeMetrics);
	json_array_append_new(resourceMetrics, rm);
	json_object_set_new(root, "resourceMetrics", resourceMetrics);
	return root;
}

static json_t *otlp_metrics_array_from_root(json_t *root)
{
	json_t *rms, *rm, *sms, *sm;

	if (!root)
		return NULL;
	rms = json_object_get(root, "resourceMetrics");
	if (!rms || json_typeof(rms) != JSON_ARRAY || json_array_size(rms) < 1)
		return NULL;
	rm = json_array_get(rms, 0);
	sms = json_object_get(rm, "scopeMetrics");
	if (!sms || json_typeof(sms) != JSON_ARRAY || json_array_size(sms) < 1)
		return NULL;
	sm = json_array_get(sms, 0);
	return json_object_get(sm, "metrics");
}

static json_t *otlp_key_value_string(const char *k, size_t klen, const char *v, size_t vlen)
{
	json_t *kv = json_object();
	json_t *valwrap = json_object();
	json_object_set_new(kv, "key", json_stringn(k, klen));
	json_object_set_new(valwrap, "stringValue", json_stringn(v, vlen));
	json_object_set_new(kv, "value", valwrap);
	return kv;
}

static void otlp_data_point_set_value(json_t *dp, metric_node *x)
{
	int8_t type = x->type;

	if (type == DATATYPE_INT)
	{
		json_object_set_new(dp, "asInt", json_integer(x->i));
	}
	else if (type == DATATYPE_UINT)
	{
		if (x->u <= (uint64_t)INT64_MAX)
		{
			json_object_set_new(dp, "asInt", json_integer((int64_t)x->u));
		}
		else
			json_object_set_new(dp, "asDouble", json_real((double)x->u));
	}
	else if (type == DATATYPE_DOUBLE)
		json_object_set_new(dp, "asDouble", json_real(x->d));
	else if (type == DATATYPE_STRING)
	{
		char *end = NULL;
		double d = strtod(x->s, &end);
		if (end != x->s && end && *end == '\0')
			json_object_set_new(dp, "asDouble", json_real(d));
		else
			json_object_set_new(dp, "asDouble", json_real(0.0));
	}
	else
		json_object_set_new(dp, "asDouble", json_real(0.0));
}

serializer_context *serializer_init(int serializer, string *str, char delimiter, string *engine, string *index_template, action_node *an)
{
	serializer_context *sc = calloc(1, sizeof(*sc));
	if (!sc)
		return NULL;

	sc->serializer = serializer;
	sc->an = an;
	if (serializer == METRIC_SERIALIZER_OPENMETRICS)
		sc->str = str;
	else if (serializer == METRIC_SERIALIZER_GRAPHITE)
		sc->str = str;
	else if (serializer == METRIC_SERIALIZER_STATSD)
		sc->str = str;
	else if (serializer == METRIC_SERIALIZER_DOGSTATSD)
		sc->str = str;
	else if (serializer == METRIC_SERIALIZER_DYNATRACE)
		sc->str = str;
	else if (serializer == METRIC_SERIALIZER_CARBON2)
		sc->str = str;
	else if (serializer == METRIC_SERIALIZER_INFLUXDB)
		sc->str = str;
	else if (serializer == METRIC_SERIALIZER_CLICKHOUSE)
	{
		sc->str = str;
		//sc->multistring = calloc(1, 1000*sizeof(void*)); // TODO: why 1000?
		//sc->ms_size = 0;
		sc->multistring = string_tokens_new();
	}
	else if (serializer == METRIC_SERIALIZER_PG)
	{
		sc->str = str;
		//sc->multistring = calloc(1, 1000*sizeof(void*)); // TODO: why 1000?
		//sc->ms_size = 0;
		sc->multistring = string_tokens_new();
	}
	else if (serializer == METRIC_SERIALIZER_JSON)
		sc->json = json_array();
	else if (serializer == METRIC_SERIALIZER_OTLP)
		sc->json = otlp_export_metrics_root_new();
	else if (serializer == METRIC_SERIALIZER_OTLP_PROTOBUF)
		sc->str = str;
	else if (serializer == METRIC_SERIALIZER_DSV)
	{
		sc->str = str;
		sc->delimiter = delimiter;
	}
	else if (serializer == METRIC_SERIALIZER_ELASTICSEARCH)
	{
		sc->str = str;
	}
	else if (serializer == METRIC_SERIALIZER_CASSANDRA)
	{
		sc->str = str;
		//sc->multistring = calloc(1, 1000*sizeof(void*));
		//sc->ms_size = 0;
		sc->multistring = string_tokens_new();
	}

	if (engine)
	{
		sc->engine = string_string_init_dup(engine);
	}

	if (index_template)
	{
		char index_name[255];
		//char index_time_template[255];
		//size_t size = strftime(index_time_template, 255, sc->index_template, localtime(&time.sec));
		//r_time time = setrtime();

		time_t rawtime;
		time(&rawtime);
		size_t size = strftime(index_name, 255, index_template->s, localtime(&rawtime));
		//strlcpy(index_name, sc->index_name->s, sc->index_name->l + 1);
		//strlcpy(index_name + sc->index_name->l, index_time_template, size + 1);
		sc->index_name = string_init_dupn(index_name, size);
	}

	sc->last_metric = "";

	return sc;
}

void serializer_do(serializer_context *sc, string *str)
{
	if (!sc || !str)
		return;

	int serializer = sc->serializer;
	if (serializer == METRIC_SERIALIZER_JSON && sc->json)
	{
		char *ret = json_dumps(sc->json, JSON_INDENT(2));
		if (ret)
		{
			string_cat(str, ret, strlen(ret));
			free(ret);
		}
	}
	else if (serializer == METRIC_SERIALIZER_OTLP && sc->json)
	{
		char *ret = json_dumps(sc->json, JSON_INDENT(2));
		if (ret)
		{
			string_cat(str, ret, strlen(ret));
			free(ret);
		}
	}
}

void serializer_free(serializer_context *sc)
{
	if (!sc)
		return;

	int serializer = sc->serializer;
	if (serializer == METRIC_SERIALIZER_JSON || serializer == METRIC_SERIALIZER_OTLP)
		json_decref(sc->json);

	if (sc->engine)
		string_free(sc->engine);

	if (sc->index_name)
		string_free(sc->index_name);

	free(sc);
}

void metric_value_serialize_string(metric_node *x, string *str)
{
	int8_t type = x->type;
	if (type == DATATYPE_INT)
		string_int(str, x->i);
	else if (type == DATATYPE_UINT)
		string_uint(str, x->u);
	else if (type == DATATYPE_DOUBLE)
		string_double(str, x->d);
	else if (type == DATATYPE_STRING)
	{
		if (x->s)
			string_cat(str, x->s, strlen(x->s));
	}
}

json_t *metric_value_serialize_json(metric_node *x)
{
	int8_t type = x->type;
	if (type == DATATYPE_INT)
		return json_integer(x->i);
	else if (type == DATATYPE_UINT)
	{
		if (x->u <= (uint64_t)INT64_MAX)
			return json_integer((json_int_t)x->u);
		return json_real((double)x->u);
	}
	else if (type == DATATYPE_DOUBLE)
	{
		if (isnan(x->d) || isinf(x->d))
			return json_real(0.0);
		return json_real(x->d);
	}
	else if (type == DATATYPE_STRING)
		return json_string(x->s ? x->s : "");

	return json_integer(0);
}

void serialize_json(metric_node *x, serializer_context *sc)
{
	labels_t *labels = x->labels;

	json_t *obj = json_object();
	json_t *jlabels = json_object();
	json_t *samples = json_array();
	while (labels)
	{
		if (labels->key_len && labels->name)
		{
			//json_t *label = json_object();
			//json_t *name = json_string(labels->name);
			json_t *value = json_string(labels->key);
			//json_array_object_insert(label, "name", name);
			//json_array_object_insert(label, "value", value);
			//json_array_object_insert(jlabels, "", label);
			json_array_object_insert(jlabels, labels->name, value);
		}
		labels = labels->next;
	}

	json_t *sample_value = metric_value_serialize_json(x);
	json_t *sample_expire = json_integer(x->expire_node ? x->expire_node->key : 0);
	r_time time = setrtime();
	json_t *sample_timestamp = json_integer(time.sec * 1000 + time.nsec / 1000000);

	json_t *sample = json_object();
	json_array_object_insert(sample, "timestamp", sample_timestamp);
	json_array_object_insert(sample, "value", sample_value);
	json_array_object_insert(sample, "expire", sample_expire);
	json_array_object_insert(samples, "", sample);

	json_array_object_insert(obj, "labels", jlabels);
	json_array_object_insert(obj, "samples", samples);
	json_array_object_insert(sc->json, "", obj);
}

void serialize_otlp(metric_node *x, serializer_context *sc)
{
	labels_t *labels = x->labels;
	json_t *metrics = otlp_metrics_array_from_root(sc->json);
	json_t *metric;
	json_t *attrs;
	json_t *dp;
	json_t *dps;
	json_t *gauge;
	r_time now;
	uint64_t tns;
	char timestr[32];

	if (!labels || !metrics)
		return;

	metric = json_object();
	char *new_name = metric_transform_name(labels->key, sc->an);
	if (new_name)
	{
		json_object_set_new(metric, "name", json_stringn(new_name, strlen(new_name)));
		free(new_name);
	}
	else
	{
		json_object_set_new(metric, "name", json_stringn(labels->key, labels->key_len));
	}

	attrs = json_array();
	for (labels = labels->next; labels; labels = labels->next)
	{
		if (labels->key_len)
			json_array_append_new(attrs, otlp_key_value_string(labels->name, labels->name_len, labels->key, labels->key_len));
	}

	now = setrtime();
	tns = now.sec * 1000000000ULL + now.nsec;
	snprintf(timestr, sizeof(timestr), "%" PRIu64, tns);

	dp = json_object();
	if (json_array_size(attrs) > 0)
		json_object_set_new(dp, "attributes", attrs);
	else
		json_decref(attrs);

	json_object_set_new(dp, "timeUnixNano", json_string(timestr));
	otlp_data_point_set_value(dp, x);

	dps = json_array();
	json_array_append_new(dps, dp);

	gauge = json_object();
	json_object_set_new(gauge, "dataPoints", dps);
	json_object_set_new(metric, "gauge", gauge);

	json_array_append_new(metrics, metric);
}


void serialize_elasticsearch(metric_node *x, serializer_context *sc)
{
	labels_t *labels = x->labels;

	json_t *obj = json_object();
	json_t *jlabels = json_array();
	//json_t *samples = json_array();

	json_t *metric_name = json_string(labels->key);

	labels = labels->next;
	while (labels)
	{
		if (labels->key_len && labels->name)
		{
			json_t *label = json_object();
			json_t *name = json_string(labels->name);
			json_t *value = json_string(labels->key);
			json_array_object_insert(label, "name", name);
			json_array_object_insert(label, "value", value);
			json_array_object_insert(jlabels, "", label);
		}
		labels = labels->next;
	}

	json_t *sample_value = metric_value_serialize_json(x);
	json_t *sample_expire = json_integer(x->expire_node ? x->expire_node->key : 0);

	//r_time time = setrtime();
	char buff[20];
	time_t rawtime;
	time(&rawtime);
	strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&rawtime));
	json_t *timestamp = json_string(buff);

	json_t *batch_index = json_object();
	json_t *batch__index = json_object();
	json_array_object_insert(batch_index, "index", batch__index);
	if (sc->index_name && sc->index_name->s)
	{
		json_t *jindex_name = json_string(sc->index_name->s);
		json_array_object_insert(batch__index, "_index", jindex_name);
	}

	//json_t *sample = json_object();
	//json_array_object_insert(sample, "metric_name", metric_name);
	//json_array_object_insert(sample, "timestamp", timestamp);
	//json_array_object_insert(sample, "value", sample_value);
	//json_array_object_insert(sample, "expire", sample_expire);
	//json_array_object_insert(samples, "", sample);
	//json_array_object_insert(obj, "samples", samples);
	
	json_array_object_insert(obj, "metric_name", metric_name);
	json_array_object_insert(obj, "timestamp", timestamp);
	json_array_object_insert(obj, "value", sample_value);
	json_array_object_insert(obj, "expire", sample_expire);

	json_array_object_insert(obj, "labels", jlabels);

	string *res = sc->str;

	char *string_index = json_dumps(batch_index, 0);
	if (string_index)
	{
		string_cat(res, string_index, strlen(string_index));
		string_cat(res, "\n", 1);
		free(string_index);
	}

	char *string_obj = json_dumps(obj, 0);
	if (string_obj)
	{
		string_cat(res, string_obj, strlen(string_obj));
		string_cat(res, "\n", 1);
		free(string_obj);
	}
	json_decref(obj);
	json_decref(batch_index);
}

void serialize_openmetrics(metric_node *x, serializer_context *sc)
{
	labels_t *labels = x->labels;

	string *res = sc->str;
	string_cat(res, labels->key, labels->key_len);
	int init = 1;

	labels = labels->next;
	while (labels)
	{
		//printf("labels %p, labels->name: '%s' (%zu), labels->key: '%s' (%zu)\n", labels, labels->name, labels->name_len, labels->key, labels->key_len);
		if (labels->key_len)
		{
			if (init)
			{
				string_cat(res, " {", 2);
				init = 0;
			}
			else
				string_cat(res, "\", ", 3);
			string_cat(res, labels->name, labels->name_len);
			string_cat(res, "=\"", 2);
			string_cat(res, labels->key, labels->key_len);
		}
		labels = labels->next;
	}

	if (!init)
		string_cat(res, "}", 1);

	string_cat(res, " ", 1);

	metric_value_serialize_string(x, res);
	string_cat(res, "\n", 1);
}

void serialize_graphite(metric_node *x, serializer_context *sc, uint64_t sec)
{
	labels_t *labels = x->labels;

	string *res = sc->str;
	string_cat(res, labels->key, labels->key_len);

	labels = labels->next;
	while (labels)
	{
		if (labels->key_len)
		{
			string_cat(res, ";", 1);
			string_cat(res, labels->name, labels->name_len);
			string_cat(res, "=\"", 2);
			string_cat(res, labels->key, labels->key_len);
		}
		labels = labels->next;
	}

	string_cat(res, " ", 1);

	metric_value_serialize_string(x, res);
	string_cat(res, " ", 1);
	string_uint(res, sec);
	string_cat(res, "\n", 1);
}

void serialize_carbon2(metric_node *x, serializer_context *sc, uint64_t sec)
{
	labels_t *labels = x->labels;

	string *res = sc->str;
	string_cat(res, labels->key, labels->key_len);

	labels = labels->next;
	while (labels)
	{
		if (labels->key_len)
		{
			string_cat(res, " ", 1);
			string_cat(res, labels->name, labels->name_len);
			string_cat(res, "=\"", 2);
			string_cat(res, labels->key, labels->key_len);
		}
		labels = labels->next;
	}

	string_cat(res, " ", 1);

	metric_value_serialize_string(x, res);
	string_cat(res, " ", 1);
	string_uint(res, sec);
	string_cat(res, "\n", 1);
}

void serialize_influxdb(metric_node *x, serializer_context *sc, uint64_t nsec)
{
	labels_t *labels = x->labels;

	string *res = sc->str;
	string_cat(res, labels->key, labels->key_len);

	labels = labels->next;
	while (labels)
	{
		if (labels->key_len)
		{
			string_cat(res, " ", 1);
			string_cat(res, labels->name, labels->name_len);
			string_cat(res, "=\"", 2);
			string_cat(res, labels->key, labels->key_len);
		}
		labels = labels->next;
	}

	string_cat(res, " ", 1);

	metric_value_serialize_string(x, res);
	string_cat(res, " ", 1);
	string_uint(res, nsec);
	string_cat(res, "\n", 1);
}

void serialize_clickhouse(metric_node *x, serializer_context *sc, uint64_t sec)
{
	labels_t *labels = x->labels;

	string *res = NULL;

	string *column_names = NULL;
	string *create_table = NULL;
	string *data = NULL;
	if (strcmp(sc->last_metric, labels->key))
	{
		string *res = string_new();
		string_tokens_string_push(sc->multistring, res);

		sc->last_metric = labels->key;
		create_table = string_new();
		string_cat(create_table, SQL_CREATE, strlen(SQL_CREATE));
		string_cat(create_table, labels->key, labels->key_len);
		string_cat(create_table, CLICKHOUSE_STANDARD_FIELDS, strlen(CLICKHOUSE_STANDARD_FIELDS));

		column_names = string_new();
		string_cat(column_names, CLICKHOUSE_INSERT_FIELDS, strlen(CLICKHOUSE_INSERT_FIELDS));

		data = string_new();
		string_cat(data, "(", 1);
		string_uint(data, sec);
		string_cat(data, ",", 1);
		metric_value_serialize_string(x, data);
	}
	else
	{
		string_cat(res, "(", 1);
		string_uint(res, sec);
		string_cat(res, ",", 1);
		metric_value_serialize_string(x, res);
	}

	labels = labels->next;

	while (labels)
	{
		if (labels->key_len)
		{
			if (data && create_table && column_names)
			{
				string_cat(data, ",'", 2);
				string_cat(data, labels->key, labels->key_len);
				string_cat(data, "'", 1);

				string_cat(create_table, ", ", 1);
				string_cat(create_table, labels->name, labels->name_len);
				string_cat(create_table, " String ", 8);

				string_cat(column_names, ",", 1);
				string_cat(column_names, labels->name, labels->name_len);
			}
			else
			{
				string_cat(res, ",'", 2);
				string_cat(res, labels->key, labels->key_len);
				string_cat(res, "'", 1);
			}
		}
		labels = labels->next;
	}

	if (data && create_table && column_names)
	{
		string_cat(create_table, ") ", 2);

		if (sc->engine)
			string_string_cat(create_table, sc->engine);
		else
			string_cat(create_table, CLICKHOUSE_ENGINE, strlen(CLICKHOUSE_ENGINE));

		string_cat(create_table, ";\n", 2);

		string_merge(res, create_table);


		string *res = string_new();
		string_tokens_string_push(sc->multistring, res);

		string_cat(res, SQL_INSERT, strlen(SQL_INSERT));
		string_cat(res, sc->last_metric, strlen(sc->last_metric));

		string_cat(column_names, ") VALUES", 8);
		string_merge(res, column_names);
		string_cat(res, "\n", 1);

		string_merge(res, data);
	}

	string_cat(res, "),\n", 3);
}

void serialize_cassandra(metric_node *x, serializer_context *sc, uint64_t sec)
{
	labels_t *labels = x->labels;

	string *res = NULL;

	string *column_names = NULL;
	string *create_table = NULL;
	string *data = NULL;
	if (strcmp(sc->last_metric, labels->key))
	{
		res = string_new();
		string_tokens_string_push(sc->multistring, res);

		sc->last_metric = labels->key;
		create_table = string_new();
		string_cat(create_table, SQL_CREATE, strlen(SQL_CREATE));
		string_cat(create_table, labels->key, labels->key_len);
		string_cat(create_table, CASSANDRA_STANDARD_FIELDS, strlen(CASSANDRA_STANDARD_FIELDS));

		column_names = string_new();
		string_cat(column_names, CASSANDRA_INSERT_FIELDS, strlen(CASSANDRA_INSERT_FIELDS));

		data = string_new();

		uuid_t uuid;
		uuid_generate_time_safe(uuid);
		char uuid_str[37];
		uuid_unparse_lower(uuid, uuid_str);

		string_cat(data, "(", 1);
		string_uint(data, sec);
		string_cat(data, ",", 1);
		string_cat(data, uuid_str, 36);
		string_cat(data, ",", 1);
		metric_value_serialize_string(x, data);
	}
	else
	{
		uuid_t uuid;
		uuid_generate_time_safe(uuid);
		char uuid_str[37];
		uuid_unparse_lower(uuid, uuid_str);

		string_cat(res, "(", 1);
		string_uint(res, sec);
		string_cat(res, ",", 1);
		string_cat(res, uuid_str, 36);
		string_cat(res, ",", 1);
		metric_value_serialize_string(x, res);
		string_cat(res, "),\n", 3);
	}

	labels = labels->next;

	if (labels) {
		while (labels)
		{
			if (labels->key_len)
			{
				if (data && create_table && column_names)
				{
					string_cat(data, ",'", 2);
					string_cat(data, labels->key, labels->key_len);
					string_cat(data, "'", 1);

					string_cat(create_table, ", ", 1);
					string_cat(create_table, labels->name, labels->name_len);
					string_cat(create_table, " TEXT ", 6);

					string_cat(column_names, ",", 1);
					string_cat(column_names, labels->name, labels->name_len);
				}
				else
				{
					string_cat(res, ",'", 2);
					string_cat(res, labels->key, labels->key_len);
					string_cat(res, "'", 1);
				}
			}
			labels = labels->next;
		}
	}

	if (data && create_table && column_names)
	{
		string_cat(create_table, ");\n", 3);

		string_merge(res, create_table);


		string *res = string_new();
		string_tokens_string_push(sc->multistring, res);

		string_cat(res, SQL_INSERT, strlen(SQL_INSERT));
		string_cat(res, sc->last_metric, strlen(sc->last_metric));

		string_cat(column_names, ") VALUES", 8);
		string_merge(res, column_names);
		string_cat(res, "\n", 1);

		string_merge(res, data);
		string_cat(res, ");\n", 3);
	}
}

void serialize_pg(metric_node *x, serializer_context *sc, uint64_t sec)
{
	labels_t *labels = x->labels;

	string *res = NULL;

	string *column_names = NULL;
	string *create_table = NULL;
	string *data = NULL;
	if (strcmp(sc->last_metric, labels->key))
	{
		string *res = string_new();
		string_tokens_string_push(sc->multistring, res);

		sc->last_metric = labels->key;
		create_table = string_new();
		string_cat(create_table, SQL_CREATE, strlen(SQL_CREATE));
		string_cat(create_table, labels->key, labels->key_len);
		string_cat(create_table, PG_STANDARD_FIELDS, strlen(PG_STANDARD_FIELDS));

		column_names = string_new();
		string_cat(column_names, PG_INSERT_FIELDS, strlen(PG_INSERT_FIELDS));

		data = string_new();
		string_cat(data, "(", 1);
		string_uint(data, sec);
		string_cat(data, ",", 1);
		metric_value_serialize_string(x, data);
	}
	else
	{
		string_cat(res, "(", 1);
		string_uint(res, sec);
		string_cat(res, ",", 1);
		metric_value_serialize_string(x, res);
	}

	labels = labels->next;

	while (labels)
	{
		if (labels->key_len)
		{
			if (data && create_table && column_names)
			{
				string_cat(data, ",'", 2);
				string_cat(data, labels->key, labels->key_len);
				string_cat(data, "'", 1);

				string_cat(create_table, ", ", 1);
				string_cat(create_table, labels->name, labels->name_len);
				string_cat(create_table, " String ", 8);

				string_cat(column_names, ",", 1);
				string_cat(column_names, labels->name, labels->name_len);
			}
			else
			{
				string_cat(res, ",'", 2);
				string_cat(res, labels->key, labels->key_len);
				string_cat(res, "'", 1);
			}
		}
		labels = labels->next;
	}

	if (data && create_table && column_names)
	{
		string_cat(create_table, ") ", 2);

		string_cat(create_table, ";\n", 2);

		string_merge(res, create_table);


		string *res = string_new();
		string_tokens_string_push(sc->multistring, res);

		string_cat(res, SQL_INSERT, strlen(SQL_INSERT));
		string_cat(res, sc->last_metric, strlen(sc->last_metric));

		string_cat(column_names, ") VALUES", 8);
		string_merge(res, column_names);
		string_cat(res, "\n", 1);

		string_merge(res, data);
	}

	string_cat(res, "),\n", 3);
}

void serialize_dsv(metric_node *x, serializer_context *sc)
{
	labels_t *labels = x->labels;

	string *res = sc->str;

	while (labels)
	{
		if (labels->key_len)
		{
			string_cat(res, labels->key, labels->key_len);
			string_cat(res, &sc->delimiter, 1);
		}
		else
			string_cat(res, &sc->delimiter, 1);
		labels = labels->next;
	}

	metric_value_serialize_string(x, res);
	string_cat(res, "\n", 1);
}

void serialize_statsd(metric_node *x, serializer_context *sc)
{
	labels_t *labels = x->labels;

	string *res = sc->str;
	uint64_t metric_str_start_position = res->l;
	char *new_name = metric_transform_name(labels->key, sc->an);
	if (new_name)
	{
		string_cat(res, new_name, strlen(new_name));
		free(new_name);
	}
	else
	{
		string_cat(res, labels->key, labels->key_len);
	}
	tag_normalizer_statsd(res->s + metric_str_start_position, labels->key_len);
	labels = labels->next;

	while (labels)
	{
		if (labels->key_len)
		{
			string_cat(res, ".", 1);

			metric_str_start_position = res->l;
			string_cat(res, labels->name, labels->name_len);
			tag_normalizer_statsd(res->s + metric_str_start_position, labels->name_len);

			string_cat(res, ".", 1);

			metric_str_start_position = res->l;
			string_cat(res, labels->key, labels->key_len);
			tag_normalizer_statsd(res->s + metric_str_start_position, labels->key_len);
		}
		labels = labels->next;
	}

	string_cat(res, ":", 1);
	metric_value_serialize_string(x, res);
	string_cat(res, "|", 1);
	if (x->type == DATATYPE_INT)
		string_cat(res, "g", 1);
	else if (x->type == DATATYPE_UINT)
		string_cat(res, "g", 1);
	else if (x->type == DATATYPE_DOUBLE)
		string_cat(res, "g", 1);
	string_cat(res, "\n", 1);
}

void serialize_dogstatsd(metric_node *x, serializer_context *sc)
{
	labels_t *labels = x->labels;

	string *res = sc->str;
	uint64_t metric_str_start_position = res->l;

	char *new_name = metric_transform_name(labels->key, sc->an);
	if (new_name)
	{
		string_cat(res, new_name, strlen(new_name));
		free(new_name);
	}
	else
	{
		string_cat(res, labels->key, labels->key_len);
	}

	tag_normalizer_statsd(res->s + metric_str_start_position, (size_t)(res->l - metric_str_start_position));

	string_cat(res, ":", 1);
	metric_value_serialize_string(x, res);
	string_cat(res, "|", 1);
	if (x->type == DATATYPE_INT)
		string_cat(res, "g", 1);
	else if (x->type == DATATYPE_UINT)
		string_cat(res, "g", 1);
	else if (x->type == DATATYPE_DOUBLE)
		string_cat(res, "g", 1);

	labels = labels->next;

	int tags_start = 1;
	while (labels)
	{
		if (labels->key_len)
		{
			if (tags_start) {
				tags_start = 0;
				string_cat(res, "|#", 2);
			}
			else
				string_cat(res, ",", 1);

			metric_str_start_position = res->l;
			string_cat(res, labels->name, labels->name_len);
			tags_normalizer_dogstatsd(res->s + metric_str_start_position, labels->name_len);

			string_cat(res, ":", 1);

			metric_str_start_position = res->l;
			string_cat(res, labels->key, labels->key_len);
			tags_normalizer_dogstatsd(res->s + metric_str_start_position, labels->key_len);

		}
		labels = labels->next;
	}
	string_cat(res, "\n", 1);

}

void serialize_dynatrace(metric_node *x, serializer_context *sc)
{
	labels_t *labels = x->labels;
	string *res = sc->str;

	uint64_t metric_str_start_position = res->l;
	char *new_name = metric_transform_name(labels->key, sc->an);
	if (new_name)
	{
		string_cat(res, new_name, strlen(new_name));
		free(new_name);
	}
	else
	{
		string_cat(res, labels->key, labels->key_len);
	}
	if (x->type == DATATYPE_INT)
	{
		string_cat(res, ".count", 6);
	}
	else if (x->type == DATATYPE_UINT)
	{
		string_cat(res, ".count", 6);
	}

	tag_normalizer_dynatrace(res->s + metric_str_start_position, labels->key_len);
	labels = labels->next;

	while (labels)
	{
		if (labels->key_len) {
			string_cat(res, ",", 1);
			metric_str_start_position = res->l;
			string_cat(res, labels->name, labels->name_len);
			tag_normalizer_dynatrace(res->s + metric_str_start_position, labels->name_len);

			string_cat(res, "=", 1);

			string_cat(res, "\"", 1);
			metric_str_start_position = res->l;
			string_cat(res, labels->key, labels->key_len);
			tag_normalizer_dynatrace(res->s + metric_str_start_position, labels->key_len);
			string_cat(res, "\"", 1);
		}
		labels = labels->next;
	}
	string_cat(res, " ", 1);
	metric_value_serialize_string(x, res);
	string_cat(res, "\n", 1);
}


void metric_node_serialize(metric_node *x, serializer_context *sc)
{
	if (sc->serializer == METRIC_SERIALIZER_OPENMETRICS)
	{
		serialize_openmetrics(x, sc);
	}
	else if (sc->serializer == METRIC_SERIALIZER_GRAPHITE)
	{
		r_time now = setrtime();
		serialize_graphite(x, sc, now.sec);
	}
	else if (sc->serializer == METRIC_SERIALIZER_STATSD)
	{
		serialize_statsd(x, sc);
	}
	else if (sc->serializer == METRIC_SERIALIZER_DOGSTATSD)
	{
		serialize_dogstatsd(x, sc);
	}
	else if (sc->serializer == METRIC_SERIALIZER_DYNATRACE)
	{
		serialize_dynatrace(x, sc);
	}
	else if (sc->serializer == METRIC_SERIALIZER_CARBON2)
	{
		r_time now = setrtime();
		serialize_carbon2(x, sc, now.sec);
	}
	else if (sc->serializer == METRIC_SERIALIZER_INFLUXDB)
	{
		r_time now = setrtime();
		uint64_t nsec = now.nsec + (now.sec * 1000000000);
		serialize_influxdb(x, sc, nsec);
	}
	else if (sc->serializer == METRIC_SERIALIZER_CLICKHOUSE)
	{
		r_time now = setrtime();
		serialize_clickhouse(x, sc, now.sec);
	}
	else if (sc->serializer == METRIC_SERIALIZER_PG)
	{
		r_time now = setrtime();
		serialize_pg(x, sc, now.sec);
	}
	else if (sc->serializer == METRIC_SERIALIZER_JSON)
	{
		serialize_json(x, sc);
	}
	else if (sc->serializer == METRIC_SERIALIZER_OTLP)
	{
		serialize_otlp(x, sc);
	}
	else if (sc->serializer == METRIC_SERIALIZER_OTLP_PROTOBUF)
	{
		otlp_protobuf_serialize(x, sc);
	}
	else if (sc->serializer == METRIC_SERIALIZER_ELASTICSEARCH)
	{
		serialize_elasticsearch(x, sc);
	}
	else if (sc->serializer == METRIC_SERIALIZER_DSV)
	{
		serialize_dsv(x, sc);
	}
	else if (sc->serializer == METRIC_SERIALIZER_CASSANDRA)
	{
		r_time now = setrtime();
		serialize_cassandra(x, sc, now.sec);
	}
}
