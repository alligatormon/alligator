#include <stdio.h>
#include "duktape.h"
#include "events/fs_read.h"
#include "lang/lang.h"

int compileJS(duk_context *ctx, const char* programBody)
{
	int success = 0;

	if (duk_pcompile_string(ctx, 0, programBody) != 0)
	{
		printf("Compile failed\n");
		printf("%s\n", duk_safe_to_string(ctx, -1));
	}
	else
	{
		duk_pcall(ctx, 0);
		success = 1;
	}
	duk_pop(ctx);

	return success;
}

char* duktape_run(lang_options *lo, char *script, char *file, char *arg, char *path, string* metrics, string *conf, string *parser_data, string *response)
{
	char *ret = NULL;

	char *metrics_str = metrics ? metrics->s : "";
	char *conf_str = conf ? conf->s : "";
	char *parser_data_str = parser_data ? parser_data->s : "";

	if (!script)
	{
		if (file)
			read_from_file(strdup(file), 0, lang_load_script, lo);
		return ret;
	}

	duk_context *ctx = duk_create_heap_default();
	if (!compileJS(ctx, script))
	{
		if (ac->log_level)
			printf("JS code compile error: key '%s' code: '%s'\n", lo->key, script);
		return ret;
	}

	if (duk_get_global_string(ctx, lo->method))
	{
		duk_push_string(ctx, arg);
		duk_push_string(ctx, metrics_str);
		duk_push_string(ctx, conf_str);
		duk_push_string(ctx, parser_data_str);

		if (duk_pcall(ctx, 4) != DUK_EXEC_SUCCESS)
		{
			duk_get_prop_string(ctx, -1, "stack");
			printf("%s\n", duk_safe_to_string(ctx, -1));
		}
		else
		{
			const char *duk_ret = duk_json_encode(ctx, -1);

			json_error_t error;
			json_t *ret_root = json_loads(duk_ret, 0, &error);
			if (!ret_root)
			{
				if (ac->log_level)
					fprintf(stderr, "return json for duktape '%s' decode error %d: %s\n", lo->key, error.line, error.text);
				return ret;
			}

			uint64_t ret_size = json_array_size(ret_root);
			if (ret_size > 0)
			{
				json_t *json_metrics = json_array_get(ret_root, 0);
				char *ret_metrics = (char*)json_string_value(json_metrics);
				if (lo->log_level > 0)
					printf("metrics result is(%s): %s\n", lo->key, ret_metrics);

				if (ret_metrics)
					ret = strdup(ret_metrics);
			}
			if (ret_size > 1)
			{
				json_t *json_response = json_array_get(ret_root, 1);
				char *ret_response = (char*)json_string_value(json_response);
				uint64_t len_response = json_string_length(json_response);
				if (lo->log_level > 0)
					printf("response result is(%s): %s\n", lo->key, ret_response);

				if (ret_response && response)
					string_cat(response, ret_response, len_response);
			}
			if (ret_size > 2)
			{
				if (lo->log_level > 0)
					printf("duktape '%s' script return more than 2 values in array, omit this result\n", lo->key);
			}

			json_decref(ret_root);
			duk_pop(ctx);
		}
	}
	else
	{
		if (ac->log_level)
			printf("JS function not found: key '%s' function: '%s', code: '%s'\n", lo->key, lo->method, script);
	}

	duk_destroy_heap(ctx);

	return ret;
}
