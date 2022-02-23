#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include "dstructures/tommy.h"
void labels_hash_insert_nocache(alligator_ht *hash, char *name, char *key);
alligator_ht *labels_dup(alligator_ht *labels);
json_t *labels_to_json(alligator_ht *labels);
int labels_hash_compare(const void* arg, const void* obj);
