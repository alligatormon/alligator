#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <uv.h>
#include "modules/modules.h"

init_func module_load(const char *so_name, const char *func)
{
	int r;

	uv_lib_t *lib = malloc(sizeof(uv_lib_t));
	printf("Loading %s with func %s\n", so_name, func);

	r = uv_dlopen(so_name, lib);
	if (r) {
		printf("Error: %s\n", uv_dlerror(lib));
		return NULL;
	}

	init_func init_plugin;
	r = uv_dlsym(lib, func, (void**) &init_plugin);
	if (r) {
		printf("dlsym error: %s\n", uv_dlerror(lib));
		return NULL;
	}

	printf("func addr %p\n", init_plugin);
	init_plugin();
	return init_plugin;
}

//int main(int argc, const char **argv)
//{
//	init_func *func = module_load(argv[1], argv[2]);
//}
