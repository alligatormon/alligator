#pragma once
#include "tommyds/tommyds/tommy.h"
#include "dstructures/ht.h"
int tommyhash_compare(const void* arg, const void* obj);
void tommy_hash_forfree(tommy_hashdyn *hash, void *funcfree);
