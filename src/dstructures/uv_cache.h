#pragma once
#include "dstructures/tommy.h"
#include <uv.h>
typedef struct alligator_cache
{
	void *data;
	uint64_t ttl;

	tommy_list *uv_cache;
	tommy_node node;
} alligator_cache;

void *alligator_cache_get(tommy_list *uv_cache, size_t size);
