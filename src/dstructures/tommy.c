#include "dstructures/tommy.h"

void tommy_hash_free_node(void *funcfree, void* arg)
{
	void (*run_func)(void*) = funcfree;
	run_func(arg);
}

void tommy_hash_forfree(tommy_hashdyn *hash, void *funcfree)
{
	tommy_hashdyn_foreach_arg(hash, tommy_hash_free_node, funcfree);
	tommy_hashdyn_done(hash);
	free(hash);
}
