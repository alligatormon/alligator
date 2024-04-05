#pragma once
#include "dstructures/tommyds/tommyds/tommy.h"
#include "events/context_arg.h"
#include "modules/modules.h"
#include "metric/query.h"
#include "query/promql.h"
#include "uv.h"

typedef struct lang_options {
	char *lang;
	char *classpath;
	char *classname;
	char *method;
	char *arg;
	uint8_t hidden_arg;
	char *script;
	size_t script_size;
	char *file;
	char *module;
	char *path;
	module_t *lib;
	void* (*func)(char *script, char *data, char *arg, char *metrics, char *conf, char *parser_data, char *response);
	context_arg *carg;
	uv_thread_t *th;
	uint64_t period;
	uint64_t log_level;
	int serializer;
	int conf;
	char *query;
	size_t query_len;
	uint8_t aggregate;
	string *response;

	uv_lib_t *func_lib;

	char *key;
	tommy_node node;
} lang_options;

void lang_push(json_t *lang, char *key);
void lang_push_options(lang_options *lo);
void lang_delete(char *key);
char* java_run(char *optionString, char* className, char *method, char *arg, string* metrics, string *conf);
char* lua_run(lang_options *lo, char* script, char *file, char *arg, string* smetrics, string *conf, string *parser_data, string *response);
char* lua_run_script(char *code, char *key, uint64_t log_level, char *method, char *arg, string* smetrics, string *conf, string *parser_data, string *response);
char* mruby_run(lang_options *lo, char* script, char *file, char *arg, string* smetrics, string *conf, string *parser_data, string *response);
char* mruby_run_script(lang_options *lo, char *code, string* metrics, string *conf, string *parser_data, string *response);
char* python_run(lang_options *lo, char* code, char *file, char *arg, char *path, string* metrics, string *conf);
char* so_run(lang_options *lo, char* script, char *file, char *data, char *arg, string* metrics, string *conf, string *data_parser, string *response);
void lang_load_script(char *script, size_t script_size, void *data, char *filename);
char* duktape_run(lang_options *lo, char *script, char *file, char *arg, char *path, string* metrics, string *conf, string *parser_data, string *response);
void lang_run(char *key, string *body, string *parser_data, string *response);
void lang_stop();
