#include "common/selector.h"
#include "metric/namespace.h"
#include <jansson.h>

serializer_context *serializer_init(int serializer, string *str, char delimiter)
{
	serializer_context *sc = calloc(1, sizeof(*sc));

	sc->serializer = serializer;
	if (serializer == METRIC_SERIALIZER_OPENMETRICS)
		sc->str = str;
	else if (serializer == METRIC_SERIALIZER_JSON)
		sc->json = json_array();
	else if (serializer == METRIC_SERIALIZER_DSV)
	{
		sc->str = str;
		sc->delimiter = delimiter;
	}

	return sc;
}

void serializer_do(serializer_context *sc, string *str)
{
	int serializer = sc->serializer;
	if (serializer == METRIC_SERIALIZER_JSON && sc->json)
	{
		char *ret = json_dumps(sc->json, JSON_INDENT(2));
		string_cat(str, ret, strlen(ret));
	}
}

void serializer_free(serializer_context *sc)
{
	int serializer = sc->serializer;
	if (serializer == METRIC_SERIALIZER_JSON)
		json_decref(sc->json);

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

	json_array_object_insert(obj, "labels", jlabels);
	json_array_object_insert(obj, "samples", samples);
	json_array_object_insert(sc->json, "", obj);
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
	else if (sc->serializer == METRIC_SERIALIZER_JSON)
	{
		serialize_json(x, sc);
	}
	else if (sc->serializer == METRIC_SERIALIZER_DSV)
	{
		serialize_dsv(x, sc);
	}
}
