#include "common/selector.h"
#include "metric/namespace.h"
#include "common/rtime.h"
#include "common/selector.h"
#include <time.h>
#include <jansson.h>
#define SQL_CREATE "CREATE TABLE IF NOT EXISTS "
#define SQL_INSERT "INSERT INTO "
#define CLICKHOUSE_ENGINE "ENGINE=MergeTree ORDER BY timestamp"
#define CLICKHOUSE_STANDARD_FIELDS "(timestamp DateTime, value Float64"
#define CLICKHOUSE_INSERT_FIELDS "(timestamp, value"
#define PG_STANDARD_FIELDS "(ts tm, value double precision"
#define PG_INSERT_FIELDS "(ts, value"

serializer_context *serializer_init(int serializer, string *str, char delimiter, string *engine, string *index_template)
{
	serializer_context *sc = calloc(1, sizeof(*sc));

	sc->serializer = serializer;
	if (serializer == METRIC_SERIALIZER_OPENMETRICS)
		sc->str = str;
	else if (serializer == METRIC_SERIALIZER_GRAPHITE)
		sc->str = str;
	else if (serializer == METRIC_SERIALIZER_CARBON2)
		sc->str = str;
	else if (serializer == METRIC_SERIALIZER_INFLUXDB)
		sc->str = str;
	else if (serializer == METRIC_SERIALIZER_CLICKHOUSE)
	{
		sc->str = str;
		sc->multistring = calloc(1, 1000*sizeof(void*)); // TODO: why 1000?
		sc->ms_size = 0;
	}
	else if (serializer == METRIC_SERIALIZER_PG)
	{
		sc->str = str;
		sc->multistring = calloc(1, 1000*sizeof(void*)); // TODO: why 1000?
		sc->ms_size = 0;
	}
	else if (serializer == METRIC_SERIALIZER_JSON)
		sc->json = json_array();
	else if (serializer == METRIC_SERIALIZER_DSV)
	{
		sc->str = str;
		sc->delimiter = delimiter;
	}
	else if (serializer == METRIC_SERIALIZER_ELASTICSEARCH)
	{
		sc->str = str;
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
	int serializer = sc->serializer;
	if (serializer == METRIC_SERIALIZER_JSON && sc->json)
	{
		printf("sc->json is %p\n", sc->json);
		char *ret = json_dumps(sc->json, JSON_INDENT(2));
		string_cat(str, ret, strlen(ret));
	}
}

void serializer_free(serializer_context *sc)
{
	int serializer = sc->serializer;
	if (serializer == METRIC_SERIALIZER_JSON)
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
		string_cat(str, x->s, strlen(x->s));
}

json_t *metric_value_serialize_json(metric_node *x)
{
	int8_t type = x->type;
	if (type == DATATYPE_INT)
		return json_integer(x->i);
	else if (type == DATATYPE_UINT)
		return json_integer(x->u);
	else if (type == DATATYPE_DOUBLE)
		return json_real(x->d);
	else if (type == DATATYPE_STRING)
		return json_string(x->s);

	return json_integer(0);
}

void serialize_json(metric_node *x, serializer_context *sc)
{
	labels_t *labels = x->labels;

	json_t *obj = json_object();
	json_t *jlabels = json_array();
	json_t *samples = json_array();
	while (labels)
	{
		if (labels->key_len)
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
	json_t *sample_expire = json_integer(x->expire_node->key);
	r_time time = setrtime();
	json_t *sample_timestamp = json_integer(time.sec * 1000 + time.nsec / 1000000);

	json_t *sample = json_object();
	json_array_object_insert(sample, "timestamp", sample_timestamp);
	json_array_object_insert(sample, "value", sample_value);
	json_array_object_insert(sample, "expire", sample_expire);
	json_array_object_insert(samples, "", sample);
	printf("serialize JSON!!!!: %p\n", sc->json);

	json_array_object_insert(obj, "labels", jlabels);
	json_array_object_insert(obj, "samples", samples);
	json_array_object_insert(sc->json, "", obj);
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
		if (labels->key_len)
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
	json_t *sample_expire = json_integer(x->expire_node->key);

	//r_time time = setrtime();
	char buff[20];
	time_t rawtime;
	time(&rawtime);
	strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&rawtime));
	json_t *timestamp = json_string(buff);

	json_t *batch_index = json_object();
	json_t *batch__index = json_object();
	json_array_object_insert(batch_index, "index", batch__index);
	json_t *jindex_name = json_string(sc->index_name->s);
	json_array_object_insert(batch__index, "_index", jindex_name);

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
	string_cat(res, string_index, strlen(string_index));
	string_cat(res, "\n", 1);
	free(string_index);

	char *string_obj = json_dumps(obj, 0);
	string_cat(res, string_obj, strlen(string_obj));
	string_cat(res, "\n", 1);
	free(string_obj);
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

	string *res;

	if(sc->ms_size)
		res = sc->multistring[sc->ms_size-1];
	else
		res = sc->multistring[sc->ms_size];

	string *column_names = NULL;
	string *create_table = NULL;
	string *data = NULL;
	if (strcmp(sc->last_metric, labels->key))
	{
		++sc->ms_size;
		res = sc->multistring[sc->ms_size-1] = string_new();

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


		++sc->ms_size;
		res = sc->multistring[sc->ms_size-1] = string_new();
		string_cat(res, SQL_INSERT, strlen(SQL_INSERT));
		string_cat(res, sc->last_metric, strlen(sc->last_metric));

		string_cat(column_names, ") VALUES", 8);
		string_merge(res, column_names);
		string_cat(res, "\n", 1);

		string_merge(res, data);
	}

	string_cat(res, "),\n", 3);
}

void serialize_pg(metric_node *x, serializer_context *sc, uint64_t sec)
{
	labels_t *labels = x->labels;

	string *res;

	if(sc->ms_size)
		res = sc->multistring[sc->ms_size-1];
	else
		res = sc->multistring[sc->ms_size];

	string *column_names = NULL;
	string *create_table = NULL;
	string *data = NULL;
	if (strcmp(sc->last_metric, labels->key))
	{
		++sc->ms_size;
		res = sc->multistring[sc->ms_size-1] = string_new();

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


		++sc->ms_size;
		res = sc->multistring[sc->ms_size-1] = string_new();
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
	else if (sc->serializer == METRIC_SERIALIZER_ELASTICSEARCH)
	{
		serialize_elasticsearch(x, sc);
	}
	else if (sc->serializer == METRIC_SERIALIZER_DSV)
	{
		serialize_dsv(x, sc);
	}
}
