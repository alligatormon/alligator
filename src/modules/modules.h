#pragma once
#include <uv.h>
typedef void (*init_func)();
init_func module_load(const char *so_name, const char *func, uv_lib_t **lib);
