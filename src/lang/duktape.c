#include <stdio.h>
#include "duktape.h"
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

char* duktape_run(lang_options *lo, char *script, char *file, char *arg, char *path, string* metrics, string *conf)
{
	char *ret = NULL;

	char *metrics_str = metrics ? metrics->s : "";
	char *conf_str = conf ? conf->s : "";

	if (!script)
	{
		if (file)
			read_from_file(strdup(file), 0, lang_load_script, lo);
		return ret;
	}

	duk_context *ctx = duk_create_heap_default();
	if (!compileJS(ctx, script))
	{
		printf("JS code compile error: key '%s' code: '%s'\n", lo->key, script);
		return ret;
	}

	if (duk_get_global_string(ctx, lo->method))
	{
		duk_push_string(ctx, arg);
		duk_push_string(ctx, metrics_str);
		duk_push_string(ctx, conf_str);

		if (duk_pcall(ctx, 3) != DUK_EXEC_SUCCESS)
		{
			duk_get_prop_string(ctx, -1, "stack");
			printf(duk_safe_to_string(ctx, -1));
		}
		else
		{
		ret = strdup(duk_get_string(ctx, -1));
		if (lo->log_level > 0)
			printf("result is(%s): %s\n", lo->key, ret);
		}
	}
	else
	{
		printf("JS function not found: key '%s' function: '%s', code: '%s'\n", lo->key, lo->method, script);
	}

	duk_pop(ctx);

	return ret;
}
