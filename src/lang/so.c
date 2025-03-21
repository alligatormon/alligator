#include "lang/type.h"
#include "events/fs_read.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

char* so_run_script(void* (*func)(char *, char*, char*, char*, char*, char*, char*, char*), lang_options *lo, char *script, char *data, char *arg, uint64_t log_level, char *key, string *metrics, string *conf, string *parser_data, string *response, char *queries)
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
	char *parser_data_str = parser_data ? parser_data->s : "";
	char *response_str = response ? response->s : "";
	char *queries_str = queries ? queries : "";

	if (func)
	{
		langlog(lo, L_INFO, "so module run '%s' '%s'\n", key, metrics_str);
		ret = func(script, data, arg, metrics_str, conf_str, parser_data_str, response_str, queries_str);
	}

	langlog(lo, L_INFO, "so module with key '%s' return:\n%s\n", key, ret);

	return ret;
}

char* so_run(lang_options *lo, char* script, char *file, char *data, char *arg, string* metrics, string *conf, string *data_parser, string *response, char *queries)
{
	char *ret = NULL;

	if (!lo->module)
	{
		printf("No defined module for key '%s', module: %s\n", lo->key, lo->module ? lo->module : "");
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
	}

	if (lo->file && !lo->script)
		read_from_file(strdup(file), 0, lang_load_script, lo);
	else
		ret = so_run_script(lo->func, lo, lo->script, data, arg, lo->log_level, lo->key, metrics, conf, data_parser, response, queries);

	return ret;
}
