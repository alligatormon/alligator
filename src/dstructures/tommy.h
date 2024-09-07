#pragma once
#include "tommyds/tommyds/tommytypes.h"
#include "tommyds/tommyds/tommyhash.h"
#include "tommyds/tommyds/tommyhashdyn.h"
#include "tommyds/tommyds/tommylist.h"

#include "dstructures/ht.h"
int tommyhash_compare(const void* arg, const void* obj);
void tommy_hash_forfree(tommy_hashdyn *hash, void *funcfree);
