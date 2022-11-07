#include "dstructures/ht.h"
#include <unistd.h>

void* alligator_ht_init(alligator_ht *h)
{
	if (!h)
		h = calloc(1, sizeof(alligator_ht));

	if (!h->ht)
		h->ht = calloc(1, sizeof(tommy_hashdyn));

	tommy_hashdyn_init(h->ht);

	return h;
}

int alligator_ht_r_lock(alligator_ht *h)
{
	uint64_t maxcnt = 65535;
	while (maxcnt--)
	{
		if (!h->rwlock)
		{
			++(h->r_lock);
			return 1;
		}
		usleep(1000);
	}

	return 0;
}

void alligator_ht_r_unlock(alligator_ht *h)
{
	--(h->r_lock);
}

int alligator_ht_rwlock(alligator_ht *h)
{
	uint64_t maxcnt = 65535;
	while (maxcnt--)
	{
		if (!h->rwlock)
		{
			if (h->r_lock)
			{
				usleep(1000 * h->r_lock);
				continue;
			}
		}

		h->rwlock = 1;
		return h->rwlock;
	}

	return 0;
}

void alligator_ht_rwunlock(alligator_ht *h)
{
	h->rwlock = 0;
}

void *alligator_ht_search(alligator_ht *h, int (*compare_func)(const void *arg, const void *arg2), const void *key, uint32_t sum)
{
	if (!h)
		return NULL;

	if (!h->ht)
		return NULL;

	void *ret = NULL;
	if (alligator_ht_r_lock(h))
	{
		ret = tommy_hashdyn_search(h->ht, compare_func, key, sum);
		alligator_ht_r_unlock(h);
	}
	return ret;
}

void *alligator_ht_remove(alligator_ht *h, int (*compare_func)(const void *arg, const void *arg2), const char *key, uint32_t sum)
{
	if (!h)
		return NULL;

	if (!h->ht)
		return NULL;

	void *ret = NULL;
	if (alligator_ht_rwlock(h))
	{
		ret = tommy_hashdyn_remove(h->ht, compare_func, key, sum);
		alligator_ht_rwunlock(h);
	}
	return ret;
}

void *alligator_ht_remove_existing(alligator_ht *h, alligator_ht_node* node)
{
	void *ret = NULL;
	uint32_t rc = alligator_ht_rwlock(h);
	if (rc)
	{
		ret = tommy_hashdyn_remove_existing(h->ht, node);
		alligator_ht_rwunlock(h);
	}

	return ret;
}

void alligator_ht_insert(alligator_ht *h, alligator_ht_node *node, void *data, uint32_t sum)
{
	if (!h)
		return;

	if (!h->ht)
		return;

	uint32_t rc = alligator_ht_rwlock(h);
	if (rc)
	{
		//tommy_hashdyn_remove_existing(h->ht, node);
		tommy_hashdyn_insert(h->ht, node, data, sum);
		alligator_ht_rwunlock(h);
	}
}

void alligator_ht_done(alligator_ht *h)
{
	uint32_t rc = alligator_ht_rwlock(h);
	if (rc)
	{
		tommy_hashdyn_done(h->ht);
		alligator_ht_rwunlock(h);
	}

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

// NO RUN write/remove inside callback!!!
void alligator_ht_foreach_arg(alligator_ht *h, tommy_foreach_arg_func *func, void *arg)
{
	if (!h)
		return;

	if (!h->ht)
		return;

	if (alligator_ht_r_lock(h))
	{
		tommy_hashdyn_foreach_arg(h->ht, func, arg);
		alligator_ht_r_unlock(h);
	}
}

// NO RUN write/remove inside callback!!!
void alligator_ht_foreach(alligator_ht *h, tommy_foreach_func *func)
{
	if (!h)
		return;

	if (!h->ht)
		return;

	if (alligator_ht_r_lock(h))
	{
		tommy_hashdyn_foreach(h->ht, func);
		alligator_ht_r_unlock(h);
	}
}
