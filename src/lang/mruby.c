#include <stdio.h>
#include <mruby.h>
#include <mruby/variable.h>
#include <mruby/compile.h>
#include <mruby/string.h>
#include "lang/lang.h"
#include "parsers/multiparser.h"
#include "events/fs_read.h"

mrb_value call_from_ruby_handler_set_metrics(mrb_state *mrb, mrb_value self)
{
	mrb_value arg;
	mrb_int ret = 0;

	mrb_get_args(mrb, "S", &arg);
	char *s_arg = mrb_str_to_cstr(mrb, arg);
	size_t len = RSTRING_LEN(arg);

	alligator_multiparser(s_arg, len, NULL, NULL, NULL);
  
	return mrb_fixnum_value(ret);
}

mrb_value call_from_ruby_handler_response(mrb_state *mrb, mrb_value self)
{
	mrb_value arg;
	mrb_int ret = 0;

	mrb_get_args(mrb, "S", &arg);
	mrb_value context = mrb_const_get(mrb, mrb_obj_value(mrb->object_class), mrb_intern_lit(mrb, "alligator_context"));

	lang_options *lo = mrb_cptr(context);

	char *s_arg = mrb_str_to_cstr(mrb, arg);
	size_t len = RSTRING_LEN(arg);

	if (lo->response && s_arg && len)
		string_cat(lo->response, s_arg, len);
  
	return mrb_fixnum_value(ret);
}

char* mruby_run_script(lang_options *lo, char *code, string* metrics, string *conf, string *parser_data, string *response)
{
	if (lo->log_level)
		printf("run mruby script %s\n", lo->key);

	mrb_state *mrb = mrb_open();
	if (!mrb)
		return NULL;

	mrb_define_method(mrb, mrb->object_class, "alligator_set_metrics", call_from_ruby_handler_set_metrics, MRB_ARGS_REQ(1));
	mrb_define_method(mrb, mrb->object_class, "alligator_set_response", call_from_ruby_handler_response, MRB_ARGS_REQ(1));

	char *metrics_str = metrics ? metrics->s : "";
	char *conf_str = conf ? conf->s : "";
	char *parser_data_str = parser_data ? parser_data->s : "";
	char *response_str = conf ? conf->s : "";
	uint64_t metrics_len = metrics ? metrics->l : 0;
	uint64_t conf_len = conf ? conf->l : 0;
	uint64_t parser_data_len = parser_data ? parser_data->l : 0;
	uint64_t response_len = response ? response->l : 0;

	char *arg = lo->arg;
	if (!arg)
		arg = "";

	size_t fullcode_size = strlen(code) + strlen(arg) + metrics_len + conf_len + parser_data_len + response_len + 1;
	char *fullcode = malloc(fullcode_size);
	snprintf(fullcode, fullcode_size, code, arg, metrics_str, conf_str, parser_data_str, response_str);

	lo->response = response;

	mrb_value obj = mrb_load_string(mrb, fullcode);
	if (mrb->exc)
	{
		mrb_close(mrb);
		printf("cannot run code\n'%s'\n", fullcode);
		free(fullcode);
		return NULL;
	}

	mrb_value context = mrb_cptr_value(mrb, lo);
	mrb_const_set(mrb, mrb_obj_value(mrb->object_class), mrb_intern_lit(mrb, "alligator_context"), context);

	mrb_value funcall_ret = mrb_funcall(mrb, obj, lo->method, 4, mrb_str_new_cstr(mrb, arg), mrb_str_new_cstr(mrb, metrics_str), mrb_str_new_cstr(mrb, conf_str), mrb_str_new_cstr(mrb, parser_data_str));

	mrb_value inspect = mrb_inspect(mrb, funcall_ret);
	if (lo->log_level)
		puts(mrb_str_to_cstr(mrb, inspect));

	mrb_close(mrb);
	free(fullcode);

	return NULL;
}

char* mruby_run(lang_options *lo, char* script, char *file, char *arg, string* smetrics, string *conf, string *parser_data, string *response)
{
	char *metrics = NULL;
	if (script)
		metrics = mruby_run_script(lo, script, smetrics, conf, parser_data, response);
	else
		read_from_file(strdup(file), 0, lang_load_script, lo);

	return metrics;
}
