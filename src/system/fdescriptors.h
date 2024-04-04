#pragma once
#include "dstructures/ht.h"

typedef struct process_fdescriptors
{
	uint32_t fd;
	char *procname;

	tommy_node node;
} process_fdescriptors;

typedef struct fd_info
{
	char *key;
	tommy_node node;
} fd_info;

typedef struct fd_info_summarize
{
	uint64_t sockets;
	uint64_t pipes;
	uint64_t files;
} fd_info_summarize;

int process_fdescriptors_compare(const void* arg, const void* obj);
void process_fdescriptors_free(void *funcarg, void* arg);
