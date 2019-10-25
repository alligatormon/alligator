#pragma once
#include "metric/expiretree.h"
#include "metric/metrictree.h"
#include "events/context_arg.h"
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
void metric_update(char *name, tommy_hashdyn *labels, void* value, int8_t type, context_arg *carg);
void metric_add(char *name, tommy_hashdyn *labels, void* value, int8_t type, context_arg *carg);
void metric_add_auto(char *name, void* value, int8_t type, context_arg *carg);
void metric_add_labels(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1);
void metric_add_labels2(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2);
void metric_add_labels3(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3);
void metric_add_labels4(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4);
void metric_add_labels5(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5);
void metric_add_labels6(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5, char *name6, char *key6);
