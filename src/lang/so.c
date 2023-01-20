#include "lang/lang.h"
#include "main.h"
extern aconf *ac;

char* so_run_script(void* (*func)(char *, char*, char*, char*, char*, char*, char*), char *script, char *data, char *arg, uint64_t log_level, char *key, string *metrics, string *conf, string *parser_data, string *response)
{
	char *ret = NULL;

	if (!data)
		data = "";

	if (!arg)
		arg = "";

	if (!script)
		script = "";

	char *metrics_str = metrics ? metrics->s : "";
	char *conf_str = conf ? conf->s : "";
	char *parser_data_str = data ? parser_data->s : "";
	char *response_str = response ? response->s : "";

	printf("func %p, script %p\n", func, script);
	if (func)
	{
		if (log_level)
			printf("so module run '%s'\n", key);
		ret = func(script, data, arg, metrics_str, conf_str, parser_data_str, response_str);
	}

	if (log_level)
		printf("so module with key '%s' return:\n%s\n", key, ret);

	return ret;
}

char* so_run(lang_options *lo, char* script, char *file, char *data, char *arg, string* metrics, string *conf, string *data_parser, string *response)
{
	char *ret = NULL;

	if (!lo->module)
	{
		printf("No defined module for key '%s', module: %s\n", lo->key, lo->module);
		return NULL;
	}

	if (!lo->func)
		lo->lib = alligator_ht_search(ac->modules, module_compare, lo->module, tommy_strhash_u32(0, lo->module));

	if (!lo->lib)
	{
		printf("No defined library in configuration\n");
		return NULL;
	}

	if (!lo->func)
	{
		lo->func = (void*)module_load(lo->lib->path, lo->method, &lo->func_lib);
		if (!lo->func)
		{
			printf("Cannot get '%s' from '%s'\n", lo->method, lo->module);
			return NULL;
		}
		return NULL;
	}

	if (lo->file && !lo->script)
		read_from_file(strdup(file), 0, lang_load_script, lo);
	else
		ret = so_run_script(lo->func, lo->script, data, arg, lo->log_level, lo->key, metrics, conf, data_parser, response);

	return ret;
}
