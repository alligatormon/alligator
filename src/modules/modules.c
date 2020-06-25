#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <uv.h>
#include "main.h"
#include "modules/modules.h"

init_func module_load(const char *so_name, const char *func, uv_lib_t **lib)
{
	extern aconf *ac;
	int r;

	*lib = malloc(sizeof(uv_lib_t));
	if (ac->log_level > 2)
		printf("Loading %s with func %s\n", so_name, func);

	r = uv_dlopen(so_name, *lib);
	if (r) {
		if (ac->log_level > 1)
			printf("Error: %s\n", uv_dlerror(*lib));
		free(*lib);
		return NULL;
	}

	init_func init_plugin;
	r = uv_dlsym(*lib, func, (void**) &init_plugin);
	if (r) {
		if (ac->log_level > 1)
			printf("dlsym error: %s\n", uv_dlerror(*lib));
		uv_dlclose(*lib);
		free(*lib);
		return NULL;
	}

	if (ac->log_level > 2)
		printf("func addr %p\n", init_plugin);
	return init_plugin;
}

int module_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((module_t*)obj)->key;
	return strcmp(s1, s2);
}
