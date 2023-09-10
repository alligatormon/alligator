#pragma once
#include "dstructures/ht.h"

typedef struct process_fdescriptors
{
	uint32_t fd;
	char *procname;

	tommy_node node;
} process_fdescriptors;

int process_fdescriptors_compare(const void* arg, const void* obj);
void process_fdescriptors_free(void *funcarg, void* arg);
