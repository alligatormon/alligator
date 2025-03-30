#include "dstructures/ht.h"
#include <unistd.h>
#include "dstructures/tommy.h"

void* alligator_ht_init(alligator_ht *h)
{
	if (!h)
		h = calloc(1, sizeof(alligator_ht));

	if (!h->ht)
		h->ht = calloc(1, sizeof(tommy_hashdyn));

	tommy_hashdyn_init(h->ht);
	pthread_rwlock_init(&h->rwlock, NULL);

	return h;
}

void *alligator_ht_search(alligator_ht *h, int (*compare_func)(const void *arg, const void *arg2), const void *key, uint32_t sum)
{
	if (!h)
		return NULL;

	if (!h->ht)
		return NULL;

	void *ret = NULL;
	pthread_rwlock_rdlock(&h->rwlock);
	ret = tommy_hashdyn_search(h->ht, compare_func, key, sum);
	pthread_rwlock_unlock(&h->rwlock);
	return ret;
}

void *alligator_ht_remove(alligator_ht *h, int (*compare_func)(const void *arg, const void *arg2), const char *key, uint32_t sum)
{
	if (!h)
		return NULL;

	if (!h->ht)
		return NULL;

	void *ret = NULL;
	pthread_rwlock_wrlock(&h->rwlock);
	ret = tommy_hashdyn_remove(h->ht, compare_func, key, sum);
	pthread_rwlock_unlock(&h->rwlock);
	return ret;
}

void *alligator_ht_remove_existing(alligator_ht *h, alligator_ht_node* node)
{
	void *ret = NULL;

	pthread_rwlock_wrlock(&h->rwlock);
	ret = tommy_hashdyn_remove_existing(h->ht, node);
	pthread_rwlock_unlock(&h->rwlock);

	return ret;
}

void alligator_ht_insert(alligator_ht *h, alligator_ht_node *node, void *data, uint32_t sum)
{
	if (!h)
		return;

	if (!h->ht)
		return;

	pthread_rwlock_wrlock(&h->rwlock);
	tommy_hashdyn_insert(h->ht, node, data, sum);
	pthread_rwlock_unlock(&h->rwlock);
}

void alligator_ht_done(alligator_ht *h)
{
	pthread_rwlock_wrlock(&h->rwlock);
	tommy_hashdyn_done(h->ht);
	pthread_rwlock_unlock(&h->rwlock);

	free(h->ht);
	h->ht = NULL;
}


size_t alligator_ht_count(alligator_ht *h)
{
	if (!h)
		return 0;

	return tommy_hashdyn_count(h->ht);
}

size_t alligator_ht_memory_usage(alligator_ht *h)
{
	if (!h)
		return 0;

	return tommy_hashdyn_memory_usage(h->ht);
}

// don't run write/remove inside callback!!!
void alligator_ht_foreach_arg(alligator_ht *h, tommy_foreach_arg_func *func, void *arg)
{
	if (!h)
		return;

	if (!h->ht)
		return;

	pthread_rwlock_rdlock(&h->rwlock);
	tommy_hashdyn_foreach_arg(h->ht, func, arg);
	pthread_rwlock_unlock(&h->rwlock);
}

// don't run write/remove inside callback!!!
void alligator_ht_foreach(alligator_ht *h, tommy_foreach_func *func)
{
	if (!h)
		return;

	if (!h->ht)
		return;

	pthread_rwlock_rdlock(&h->rwlock);
	tommy_hashdyn_foreach(h->ht, func);
	pthread_rwlock_unlock(&h->rwlock);
}

void alligator_ht_forfree(alligator_ht *h, void *funcfree)
{
	if (!h)
		return;

	if (!h->ht)
		return;

	pthread_rwlock_rdlock(&h->rwlock);
	tommy_hash_forfree(h->ht, funcfree);
	pthread_rwlock_unlock(&h->rwlock);
}

uint32_t alligator_ht_strhash(const char *buf, uint32_t len, uint32_t initial) {
	return tommy_strhash_u32(initial, buf);
}
