#pragma once
#include "dstructures/tommy.h"

typedef struct reject_item
{
	char *value;
	tommy_node node;
} reject_item;

typedef struct reject_t
{
	char *name;
	tommy_hashdyn *ritem;

	tommy_node node;
} reject_t;

int reject_hash_compare(const void* arg, const void* obj);
