#pragma once
#include "dstructures/ht.h"

typedef struct process_fdescriptors
{
	uint32_t fd;
	char *procname;

	tommy_node node;
} process_fdescriptors;

