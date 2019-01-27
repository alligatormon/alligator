#pragma once
#define ALLIGATOR_DATATYPE_STRING 3
#define ALLIGATOR_DATATYPE_INT 2
#define ALLIGATOR_DATATYPE_DOUBLE 1
#define ALLIGATOR_DATATYPE_NONE 0
#include "tommyds/tommyds/tommy.h"
#include "dstructures/rbtree.h"
#include "common/selector.h"

typedef struct kv_metric
{
	tommy_node node;
	char *key;
	rb_tree *tree;
	rb_node *res;
} kv_metric;

typedef struct namespace_struct
{
	tommy_node node;
	char *key;
	tommy_hashdyn *metric;
} namespace_struct;

void metric_labels_add(char *name, alligator_labels *lbl, void* value, int datatype, char *ns_use);
void metric_labels_print(stlen *str);
void metric_labels_add_lbl3(char *mname, void* value, int datatype, char *ns_use, char *lblname1, char *lblkey1, char *lblname2, const char *lblkey2, char *lblname3, const char *lblkey3);
void metric_labels_add_lbl2(char *mname, void* value, int datatype, char *ns_use, char *lblname1, char *lblkey1, char *lblname2, const char *lblkey2);
void metric_labels_add_lbl(char *mname, void* value, int datatype, char *ns_use, char *lblname1, const char *lblkey1);
void metric_labels_add_auto(char *mname, void* value, int datatype, char *ns_use);
void metric_labels_add_auto_prefix(char *mname, char *prefix, void* value, int datatype, char *ns_use);
