#pragma once
#include "events/context_arg.h"
#include "cluster/type.h"
cluster_node *get_cluster_node_from_carg(context_arg *carg);
cluster_node* cluster_get(char *name);
