#include "dstructures/uv_cache.h"
#include "common/rtime.h"
#include "main.h"
#define ALLIGATOR_TIMER_SECONDS_EXPIRE 20
tommy_list *alligator_cache_list_init()
{
	tommy_list *uv_cache = malloc(sizeof(*uv_cache));
	tommy_list_init(uv_cache);

	return uv_cache;
}

//int alligator_cache_lock(alligator_cache *acache)
//{
//	if (acache->lock)
//		return 0
//}

void *alligator_cache_get(tommy_list *uv_cache, size_t size)
{
	tommy_node *tnode = tommy_list_tail(uv_cache);
	if (!tnode)
		return calloc(1, size);

	alligator_cache *acache = tnode->data;
	//while (!alligator_cache_lock(acache))
	//{
	//	tnode = tnode->prev;
	//	acache = tnode->data;
	//}

	//if (!acache)
	//	return calloc(1, size);

	tommy_list_remove_existing(uv_cache, &acache->node);
	//printf("cache get %zu\n", tommy_list_count(uv_cache));

	void *data = acache->data;
	printf("free acache %p with size %zu\n", acache, sizeof(*acache));
	free(acache);

	return data;
}

int alligator_cache_push(tommy_list *uv_cache, void *data)
{
	if (!uv_cache)
		return 0;

	if (!data)
		return 0;

	alligator_cache *acache = calloc(1, sizeof(*acache));
	printf("create acache %p with size %zu\n", acache, sizeof(*acache));

	acache->data = data;
        r_time time = setrtime();
	acache->ttl = time.sec + ALLIGATOR_TIMER_SECONDS_EXPIRE;
	acache->uv_cache = uv_cache;

	tommy_list_insert_tail(uv_cache, &acache->node, acache);

	return 1;
}

void alligator_cache_foreach_free(void *arg, void *obj)
{
	alligator_cache *acache = obj;
	tommy_list *uv_cache = acache->uv_cache;

	uint64_t sec = INT64_MAX;
	if (arg)
		sec = *(uint64_t*)arg;

	if (acache->ttl <= sec)
	{
		if (sec == INT64_MAX)
		{
			printf("remove existing %p %p->%p\n", uv_cache, acache, acache->node);
			tommy_list_remove_existing(uv_cache, &acache->node);
		}

		free(acache->data);
		printf("free acache %p with size %zu\n", acache, sizeof(*acache));
		free(acache);
	}
	else
	{
		printf("ttl !!! %"u64" != %"u64"\n", acache->ttl, sec);
	}
}

void alligator_cache_periodical_free(tommy_list *uv_cache)
{
        r_time time = setrtime();
	time.sec += ALLIGATOR_TIMER_SECONDS_EXPIRE;
	tommy_list_foreach_arg (uv_cache, alligator_cache_foreach_free, &time.sec);
}

void alligator_cache_full_free(tommy_list *uv_cache)
{
	tommy_list_foreach_arg (uv_cache, alligator_cache_foreach_free, NULL);
}
