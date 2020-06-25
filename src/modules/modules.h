#pragma once
#include <uv.h>
#include "dstructures/tommyds/tommyds/tommy.h"
typedef struct module_t {
	char *key;
	char *path;
	tommy_node node;
} module_t;

typedef void (*init_func)();
init_func module_load(const char *so_name, const char *func, uv_lib_t **lib);
int module_compare(const void* arg, const void* obj);
