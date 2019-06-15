#pragma once
#include "metric/expiretree.h"
#include "metric/metrictree.h"
typedef struct namespace_struct
{
	tommy_node node;
	char *key;
	expire_tree *expiretree;
	metric_tree *metrictree;
} namespace_struct;

#define DATATYPE_LIST_STRING 8
#define DATATYPE_LIST_UINT 7
#define DATATYPE_LIST_INT 6
#define DATATYPE_LIST_DOUBLE 5
#define DATATYPE_STRING 4
#define DATATYPE_UINT 3
#define DATATYPE_INT 2
#define DATATYPE_DOUBLE 1
#define DATATYPE_NONE 0
#define METRIC_STORAGE_BUFFER_DEFAULT 1024
