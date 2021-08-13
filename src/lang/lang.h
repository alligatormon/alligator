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
	char *script;
	size_t script_size;
	char *file;
	char *module;
	char *path;
	module_t *lib;
	void* (*func)(char *script, char *data, char *arg, char *metrics, char *conf);
	context_arg *carg;
	uv_thread_t *th;
	uint64_t period;
	uint64_t log_level;
	int serializer;
	int conf;
	char *query;
	size_t query_len;

	uv_lib_t *func_lib;

	char *key;
	tommy_node node;
} lang_options;

void lang_push(lang_options *lo);
void lang_delete(char *key);
char* java_run(char *optionString, char* className, char *method, char *arg, string* metrics, string *conf);
char* lua_run(lang_options *lo, char* script, char *file, char *arg, string* metrics, string *conf);
char* mruby_run(lang_options *lo, char* script, char *file, char *arg, string* metrics, string *conf);
char* python_run(lang_options *lo, char* code, char *file, char *arg, char *path, string* metrics, string *conf);
char* so_run(lang_options *lo, char* script, char *file, char *data, char *arg, string* metrics, string *conf);
void lang_load_script(char *script, size_t script_size, void *data, char *filename);
char* duktape_run(lang_options *lo, char *script, char *file, char *arg, char *path, string* metrics, string *conf);
