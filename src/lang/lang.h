#pragma once
#include "dstructures/tommyds/tommyds/tommy.h"
#include "events/context_arg.h"

typedef struct lang_options {
	char *lang;
	char *classpath;
	char *classname;
	char *method;
	char *arg;
	context_arg *carg;

	char *key;
	tommy_node node;
} lang_options;

void lang_push(lang_options *lo);
void lang_delete(char *key);
char* java_run(char *optionString, char* className, char *method, char *arg);
