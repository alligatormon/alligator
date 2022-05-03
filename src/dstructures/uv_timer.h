#pragma once
#include "dstructures/tommy.h"
#include <uv.h>
typedef struct alligator_timer
{
	uv_timer_t *timer;
	uint64_t ttl;

	tommy_list *altimer;
	tommy_node node;
} alligator_timer;
