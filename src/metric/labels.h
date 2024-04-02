#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include "dstructures/tommy.h"
#include "query/promql.h"
void labels_hash_insert_nocache(alligator_ht *hash, char *name, char *key);
alligator_ht *labels_dup(alligator_ht *labels);
json_t *labels_to_json(alligator_ht *labels);
int labels_hash_compare(const void* arg, const void* obj);
void labels_merge(alligator_ht *dst, alligator_ht *src);
void labels_hash_free(alligator_ht *hash);
void labels_hash_insert(alligator_ht *hash, char *name, char *key);
void metric_query_gen (char *namespace, metric_query_context *mqc, char *new_name, char *action_name);
