#pragma once
#include "dstructures/tommy.h"
#include "common/selector.h"
#include "main.h"
#include "metric/namespace.h"
#include "query/promql.h"
void metric_str_build_query (char *namespace, string *str, char *name, tommy_hashdyn *hash, int func, string *groupkey, int serializer, char delimiter);
string* metric_query_deserialize(size_t init_size, metric_query_context *mqc, int serializer, char delimiter, char *namespace);
