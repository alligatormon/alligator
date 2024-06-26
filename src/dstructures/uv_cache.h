#pragma once
#include "dstructures/tommy.h"
#include <uv.h>
typedef struct alligator_cache
{
	void *data;
	uint64_t ttl;

	uint8_t lock;
	tommy_list *uv_cache;
	tommy_node node;
} alligator_cache;

void *alligator_cache_get(tommy_list *uv_cache, size_t size);
int alligator_cache_push(tommy_list *uv_cache, void *data);
void alligator_cache_full_free(tommy_list *uv_cache);
