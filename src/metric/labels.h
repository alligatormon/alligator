#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include "dstructures/tommy.h"
void labels_hash_insert_nocache(tommy_hashdyn *hash, char *name, char *key);
tommy_hashdyn *labels_dup(tommy_hashdyn *labels);
json_t *labels_to_json(tommy_hashdyn *labels);
