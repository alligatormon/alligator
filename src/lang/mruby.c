#include <stdio.h>
#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/string.h>
#include "lang/lang.h"

char* mruby_run_script(lang_options *lo, char *code, string* metrics, string *conf)
{
	if (lo->log_level)
		printf("run mruby script %s\n", lo->key);

	mrb_state *mrb = mrb_open();
	if (!mrb)
		return NULL;

	char *metrics_str = metrics ? metrics->s : "";
	char *conf_str = conf ? conf->s : "";
	uint64_t metrics_len = metrics ? metrics->l : 0;
	uint64_t conf_len = conf ? conf->l : 0;

	//code = "5.times { puts 'mruby is awesome!' }; return 'test'";
	//code = "def helloworld() return'testtest' end";

	char *arg = lo->arg;
	if (!arg)
		arg = "";

	size_t fullcode_size = strlen(code) + strlen(arg) + metrics_len + conf_len + 1;
	char *fullcode = malloc(fullcode_size);
	snprintf(fullcode, fullcode_size, code, arg, metrics_str, conf_str);

	mrb_value s = mrb_load_string(mrb, fullcode);
	//printf("(%zu/%zu/%zu) full code \"%s\", code \"%s\"\n", fullcode_size, strlen(code), strlen(arg), fullcode, code);
	//mrb_value s = mrb_load_string(mrb, "helloworld()");
	char *ret = NULL;
	if (mrb_type(s) == MRB_TT_STRING)
	{
		char *ret_from_mruby = RSTRING_PTR(s);
		if (ret_from_mruby)
			ret = strdup(ret_from_mruby);
		if (lo->log_level)
			printf("return from mruby '%s': '%s'\n", lo->key, ret);
	}
	else
	{
		if (lo->log_level)
			printf("return from mruby '%s' not string: %d, errror\n", lo->key, mrb_type(s));
	}

	mrb_close(mrb);
	free(fullcode);

	return ret;
}

char* mruby_run(lang_options *lo, char* script, char *file, char *arg, string* smetrics, string *conf)
{
	char *metrics = NULL;
	if (script)
		metrics = mruby_run_script(lo, script, smetrics, conf);
	else
		read_from_file(strdup(file), 0, lang_load_script, lo);

	return metrics;
}

