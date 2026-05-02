#include "main.h"
#include "dstructures/ht.h"
#include "action/type.h"
#include "parsers/multiparser.h"
#include "common/json_query.h"
#include "common/logs.h"

void action_parse_add_label(action_node *an, json_t *root) {
    json_t *json_add_label = json_object_get(root, "add_label");

    const char *name;
    json_t *jkey;
    json_object_foreach(json_add_label, name, jkey)
    {
        char *key = (char*)json_string_value(jkey);

        if (!an->labels)
        {
            an->labels = alligator_ht_init(NULL);
        }

        labels_hash_insert_nocache(an->labels, (char*)name, key);
    }
}

void action_push(json_t *action)
{
	json_t *jname = json_object_get(action, "name");
	if (!jname)
	{
		glog(L_ERROR, "create action failed, 'name' is empty\n");
		return;
	}
	char *name = (char*)json_string_value(jname);
	if (!name)
	{
		glog(L_ERROR, "create action failed, 'name' is not a string\n");
		return;
	}

	//json_t *jdatasource = json_object_get(action, "datasource");
	//if (!jdatasource)
	//{
	//	glog(L_ERROR, "create action failed, 'datasource' is empty\n");
	//	return;
	//}
	//char *datasource = (char*)json_string_value(jdatasource);

	action_node *an = calloc(1, sizeof(*an));

	json_t *jexpr = json_object_get(action, "expr");
	if (jexpr)
	{
		char *expr = (char*)json_string_value(jexpr);
		if (expr) {
			an->expr = strdup(expr);
			an->expr_len = json_string_length(jexpr);
		}
	}
	else {
		glog(L_ERROR, "not found specified 'expr' for action: '%s'\n", name);
	}

	json_t *jns = json_object_get(action, "ns");
	if (jns)
	{
		char *ns = (char*)json_string_value(jns);
		an->ns = strdup(ns);
	}

	json_t *jwork_dir = json_object_get(action, "work_dir");
	if (jwork_dir)
	{
		an->work_dir = string_init_dupn((char*)json_string_value(jwork_dir), json_string_length(jwork_dir));
	}

	json_t *jengine = json_object_get(action, "engine");
	if (jengine)
	{
		an->engine = string_init_dupn((char*)json_string_value(jengine), json_string_length(jengine));
	}

	json_t *jdry_run = json_object_get(action, "dry_run");
	if (json_is_true(jdry_run))
	{
		an->dry_run = 1;
	}

	json_t *jmetric_name_transform_pattern = json_object_get(action, "metric_name_transform_pattern");
	if (jmetric_name_transform_pattern)
	{
		an->metric_name_transform_pattern = strdup(json_string_value(jmetric_name_transform_pattern));
	}

	json_t *jmetric_name_transform_replacement = json_object_get(action, "metric_name_transform_replacement");
	if (jmetric_name_transform_replacement)
	{
		an->metric_name_transform_replacement = strdup(json_string_value(jmetric_name_transform_replacement));
	}

	json_t *jlog_level = json_object_get(action, "log_level");
	if (jlog_level)
	{
		an->log_level_defined = 1;
		if (json_typeof(jlog_level) == JSON_STRING)
			an->log_level = (int)get_log_level_by_name(json_string_value(jlog_level), json_string_length(jlog_level));
		else
			an->log_level = (int)json_integer_value(jlog_level);
	}

	json_t *jindex_template = json_object_get(action, "index_template");
	if (jindex_template)
	{
		an->index_template = string_init_dupn((char*)json_string_value(jindex_template), json_string_length(jindex_template));
	}


	action_parse_add_label(an, action);

	an->follow_redirects = config_get_intstr_json(action, "follow_redirects");

	json_t *jenv = json_object_get(action, "env");
	if (jenv && json_is_object(jenv))
		an->env = env_struct_parser(action);

	//json_t *jtype = json_object_get(action, "type");
	//if (jtype)
	//{
	//	char *type_str = (char*)json_string_value(jtype);
	//	if (!strcmp(type_str, "shell"))
	//		an->type = ACTION_TYPE_SHELL;
	//}
	an->serializer = METRIC_SERIALIZER_OPENMETRICS;

	json_t *jserializer = json_object_get(action, "serializer");
	if (jserializer)
	{
		char *srlz = (char*)json_string_value(jserializer);
		if (!srlz)
		{
			glog(L_ERROR, "action %s error: serializer is not a string, use openmetrics by default\n", name);
			srlz = "openmetrics";
		}
		if(!strcmp(srlz, "json"))
			an->serializer = METRIC_SERIALIZER_JSON;
		else if(!strcmp(srlz, "otlp"))
		{
			an->serializer = METRIC_SERIALIZER_OTLP;
			an->content_type_json = 1;
			an->parser = otlp_response_catch;
			an->parser_name = strdup("otlp_response_catch");
			printf("an %p serializer is '%s', %p, '%s'\n", an, srlz, an->parser, an->parser_name);
		}
		else if(!strcmp(srlz, "otlp_protobuf"))
		{
			an->serializer = METRIC_SERIALIZER_OTLP_PROTOBUF;
			an->content_type_protobuf = 1;
			an->parser = otlp_protobuf_response_catch;
			an->parser_name = strdup("otlp_protobuf_response_catch");
		}
		else if(!strcmp(srlz, "dsv"))
			an->serializer = METRIC_SERIALIZER_DSV;
		else if(!strcmp(srlz, "graphite"))
			an->serializer = METRIC_SERIALIZER_GRAPHITE;
		else if(!strcmp(srlz, "statsd"))
			an->serializer = METRIC_SERIALIZER_STATSD;
		else if(!strcmp(srlz, "dogstatsd"))
			an->serializer = METRIC_SERIALIZER_DOGSTATSD;
		else if(!strcmp(srlz, "dynatrace")) {
			an->serializer = METRIC_SERIALIZER_DYNATRACE;
			an->content_type_json = 1;
			an->parser = dynatrace_response_catch;
			an->parser_name = strdup("dynatrace_response_catch");
			printf("an %p serializer is '%s', %p, '%s'\n", an, srlz, an->parser, an->parser_name);
		}
		else if(!strcmp(srlz, "carbon2"))
			an->serializer = METRIC_SERIALIZER_CARBON2;
		else if(!strcmp(srlz, "influxdb"))
			an->serializer = METRIC_SERIALIZER_INFLUXDB;
		else if(!strcmp(srlz, "clickhouse"))
			an->serializer = METRIC_SERIALIZER_CLICKHOUSE;
		else if(!strcmp(srlz, "postgresql"))
			an->serializer = METRIC_SERIALIZER_PG;
		else if(!strcmp(srlz, "mongodb"))
			an->serializer = METRIC_SERIALIZER_MONGODB;
		else if(!strcmp(srlz, "cassandra"))
			an->serializer = METRIC_SERIALIZER_CASSANDRA;
		else if(!strcmp(srlz, "elasticsearch"))
		{
			an->serializer = METRIC_SERIALIZER_ELASTICSEARCH;
			an->content_type_json = 1;
			an->parser = elasticsearch_response_catch;
			an->parser_name = strdup("elasticsearch_response_catch");
		}
		else
		{
			glog(L_ERROR, "action %s error: unknown serializator '%s', use openmetrics by default\n", name, srlz);
		}
	}

	an->name = strdup(name);
	//an->datasource = strdup(datasource);

	glog(L_INFO, "create action node %p name '%s', expr '%s'\n", an, an->name, an->expr);

	alligator_ht_insert(ac->action, &(an->node), an, tommy_strhash_u32(0, an->name));
}
