#include "common/logs.h"
#include "grok/type.h"
#include "main.h"
#include <string.h>
#include <pthread.h>

/* Serializes get/put/del destroy and pattern table swap vs expand. */
static pthread_mutex_t grok_life = PTHREAD_MUTEX_INITIALIZER;

void grok_life_lock(void)
{
	pthread_mutex_lock(&grok_life);
}

void grok_life_unlock(void)
{
	pthread_mutex_unlock(&grok_life);
}

int grok_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((grok_node*)obj)->name;
	return strcmp(s1, s2);
}

int grokds_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((grok_ds*)obj)->key;
	return strcmp(s1, s2);
}

grok_ds* grok_get(char *key)
{
	if (!key) {
		glog(L_ERROR, "Not specified grok key\n");
		return NULL;
	}

	pthread_mutex_lock(&grok_life);
	grok_ds *gps = alligator_ht_search(ac->grok, grokds_compare, key, tommy_strhash_u32(0, key));
	if (!gps || gps->zombie)
		gps = NULL;
	else
		__atomic_fetch_add(&gps->refs, 1, __ATOMIC_ACQ_REL);
	pthread_mutex_unlock(&grok_life);
	return gps;
}

void grok_put(grok_ds *gps)
{
	if (!gps)
		return;
	int left = __atomic_fetch_sub(&gps->refs, 1, __ATOMIC_ACQ_REL) - 1;
	if (left != 0)
		return;

	/* Single destroy owner with stop path (destroy_claimed). */
	if (gps->zombie && __atomic_load_n(&gps->refs, __ATOMIC_ACQUIRE) == 0) {
		if (!__atomic_exchange_n(&gps->destroy_claimed, 1, __ATOMIC_ACQ_REL))
			grok_ds_destroy(gps);
	}
}

grok_node *grok_get_node(grok_ds *gps, char *name)
{
	if (!gps)
		return NULL;

	grok_node *gn = alligator_ht_search(gps->hash, grok_compare, name, tommy_strhash_u32(0, name));
	if (gn)
		return gn;
	else
		return NULL;
}
