#include "lang/type.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

int lang_push(json_t *lang)
{

	json_t *lang_key = json_object_get(lang, "key");
	if (!lang_key)
	{
		glog(L_ERROR, "no key for lang \n");
		return 0;
	}
	char *key = (char*)json_string_value(lang_key);

    lang_options *lo = lang_get(key);
    if (lo) {
		glog(L_ERROR, "lang key '%s' already exists\n", key);
		return 0;
    }

	json_t *lang_name = json_object_get(lang, "lang");
	if (!lang_name)
	{
		glog(L_ERROR, "action error: field 'name' not defined for key '%s'\n", key);
		return 0;
	}
	char *slang_name = (char*)json_string_value(lang_name);

	json_t *lang_method = json_object_get(lang, "method");
	char *slang_method = (char*)json_string_value(lang_method);

	json_t *lang_arg = json_object_get(lang, "arg");
	char *slang_arg = (char*)json_string_value(lang_arg);

	json_t *lang_script = json_object_get(lang, "script");
	char *slang_script = (char*)json_string_value(lang_script);

	json_t *lang_file = json_object_get(lang, "file");
	char *slang_file = (char*)json_string_value(lang_file);

	json_t *lang_module = json_object_get(lang, "module");
	char *slang_module = (char*)json_string_value(lang_module);

	json_t *lang_query = json_object_get(lang, "query");
	char *slang_query = (char*)json_string_value(lang_query);

	uint8_t hidden_arg = 0;
	json_t *slang_hidden_arg = json_object_get(lang, "hidden_arg");
	if (slang_hidden_arg) {
		char *strlang_hidden_arg = (char*)json_string_value(slang_hidden_arg);
		if (strlang_hidden_arg)
			if (!strcmp(strlang_hidden_arg, "true"))
				hidden_arg = 1;
	}

	uint64_t slang_log_level = 0;
	json_t *lang_log_level = json_object_get(lang, "log_level");
	if (lang_log_level)
	{
		int type = json_typeof(lang_log_level);
		if (type == JSON_INTEGER)
			slang_log_level = json_integer_value(lang_log_level);
		else if (type == JSON_STRING)
			slang_log_level = get_log_level_by_name(json_string_value(lang_log_level), json_string_length(lang_log_level));
	}

	lo = calloc(1, sizeof(*lo));

	lo->serializer = METRIC_SERIALIZER_OPENMETRICS;
	json_t *jserializer = json_object_get(lang, "serializer");
	if (jserializer)
	{
		if(!strcmp(json_string_value(jserializer), "json"))
			lo->serializer = METRIC_SERIALIZER_JSON;
		else if(!strcmp(json_string_value(jserializer), "dsv"))
			lo->serializer = METRIC_SERIALIZER_DSV;
	}

	json_t *lang_conf = json_object_get(lang, "conf");
	if (lang_conf)
	{
		const char *sconf = json_string_value(lang_conf);
		if (!strcmp(sconf, "on"))
			lo->conf = 1;
	}

	json_t *lang_aggregate = json_object_get(lang, "aggregate");
	if (lang_aggregate)
	{
		const char *saggregate = json_string_value(lang_aggregate);
		if (!strcmp(saggregate, "on"))
			lo->aggregate = 1;
	}


	lo->lang = strdup(slang_name);
	lo->key = strdup(key);

	if (slang_method)
		lo->method = strdup(slang_method);
	if (slang_arg)
		lo->arg = strdup(slang_arg);
	if (slang_hidden_arg)
		lo->hidden_arg = hidden_arg;
	if (slang_script)
		lo->script = strdup(slang_script);
	if (slang_file)
		lo->file = strdup(slang_file);
	if (slang_module)
		lo->module = strdup(slang_module);
	if (slang_query)
		lo->query = strdup(slang_query);

	if (slang_log_level)
		lo->log_level = slang_log_level;
	else
		lo->log_level = ac->log_level;

	langlog(lo, L_INFO, "lang push key %s\n", lo->key);

	alligator_ht_insert(ac->lang_aggregator, &(lo->node), lo, tommy_strhash_u32(0, lo->key));

    return 1;
}

void lang_push_options(lang_options *lo)
{
	langlog(lo, L_INFO, "lang push key %s\n", lo->key);

	alligator_ht_insert(ac->lang_aggregator, &(lo->node), lo, tommy_strhash_u32(0, lo->key));
}
