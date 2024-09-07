#pragma once
#include "dstructures/tommy.h"
#include "events/context_arg.h"
#include "modules/modules.h"
#include "metric/query.h"
#include "query/promql.h"
#include "uv.h"

typedef struct lang_options {
	char *lang;
	char *method;
	char *arg;
	uint8_t hidden_arg;
	char *script;
	size_t script_size;
	char *file;
	char *module;
	module_t *lib;
	void* (*func)(char *script, char *data, char *arg, char *metrics, char *conf, char *parser_data, char *response);
	context_arg *carg;
	uv_thread_t *th;
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

int lang_push(json_t *lang);
void lang_push_options(lang_options *lo);
void lang_delete(json_t *lang);
char* java_run(char *optionString, char* className, char *method, char *arg, string* metrics, string *conf);
char* lua_run(lang_options *lo, char* script, char *file, char *arg, string* smetrics, string *conf, string *parser_data, string *response);
char* mruby_run(lang_options *lo, char* script, char *file, char *arg, string* smetrics, string *conf, string *parser_data, string *response);
char* mruby_run_script(lang_options *lo, char *code, string* metrics, string *conf, string *parser_data, string *response);
char* python_run(lang_options *lo, char* code, char *file, char *arg, char *path, string* metrics, string *conf);
char* so_run(lang_options *lo, char* script, char *file, char *data, char *arg, string* metrics, string *conf, string *data_parser, string *response);
void lang_load_script(char *script, size_t script_size, void *data, char *filename);
char* duktape_run(lang_options *lo, char *script, char *file, char *arg, string* metrics, string *conf, string *parser_data, string *response);
void lang_run(char *key, string *body, string *parser_data, string *response);
void lang_stop();
int lang_compare(const void* arg, const void* obj);
lang_options* lang_get(char *key);
void langlog(lang_options *lo, int priority, const char *format, ...);
void lang_delete_key(char *key);
