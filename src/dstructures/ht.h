#pragma once
#include "dstructures/tommy.h"
#include <pthread.h>
typedef struct alligator_ht
{
	tommy_hashdyn *ht;
	//uint32_t r_lock;
	pthread_rwlock_t rwlock;
} alligator_ht;

typedef tommy_hashdyn_node alligator_ht_node;

void *alligator_ht_search(alligator_ht *h, int (*compare_func)(const void *arg, const void *arg2), const void *key, uint32_t sum);
void alligator_ht_done(alligator_ht *h);
void alligator_ht_insert(alligator_ht *h, alligator_ht_node *node, void *data, uint32_t sum);
void *alligator_ht_remove_existing(alligator_ht *h, alligator_ht_node* node);
size_t alligator_ht_count(alligator_ht *h);
void alligator_ht_foreach_arg(alligator_ht *h, tommy_foreach_arg_func *func, void *arg);
void* alligator_ht_init(alligator_ht *h);
void alligator_ht_foreach(alligator_ht *h, tommy_foreach_func *func);
size_t alligator_ht_memory_usage(alligator_ht *h);
void *alligator_ht_remove(alligator_ht *h, int (*compare_func)(const void *arg, const void *arg2), const char *key, uint32_t sum);
void alligator_ht_forfree(alligator_ht *h, void *funcfree);
uint32_t alligator_ht_strhash(const char *buf, uint32_t len, uint32_t initial);
