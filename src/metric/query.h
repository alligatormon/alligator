#pragma once
#include "dstructures/tommy.h"
#include "common/selector.h"
#include "main.h"
#include "metric/namespace.h"
#include "query/promql.h"
void metric_str_build_query (char *namespace, string *str, char *name, alligator_ht *hash, int func, string *groupkey, int serializer, char delimiter, string ***ms, uint64_t *ms_size, string *engine, string *index_template);
string* metric_query_deserialize(size_t init_size, metric_query_context *mqc, int serializer, char delimiter, char *namespace, string ***ms, uint64_t *ms_size, string *engine, string *index_template);
