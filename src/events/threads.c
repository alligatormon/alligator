#include <common/logs.h>
#include <events/context_arg.h>
#include <main.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern aconf *ac;

typedef struct threaded_loop_work {
	tommy_node node;
	void (*fn)(void *);
	void *arg;
} threaded_loop_work;

static void threaded_loop_slot_enqueue(threaded_loop_slot *slot, void (*fn)(void *), void *arg)
{
	threaded_loop_work *w;

	if (!slot || slot->work_lock_ready != 2 || !fn)
		return;

	w = malloc(sizeof(*w));
	if (!w)
		return;
	w->fn = fn;
	w->arg = arg;

	uv_mutex_lock(&slot->work_lock);
	tommy_list_insert_tail(&slot->work_list, &w->node, w);
	uv_mutex_unlock(&slot->work_lock);
	uv_async_send(&slot->async);
}

typedef struct {
	uv_loop_t *loop;
	threaded_loop_slot *found;
} threaded_loop_slot_lookup;

static void threaded_loop_slot_lookup_cb(void *arg, void *obj)
{
	threaded_loop_slot_lookup *ctx = arg;
	threaded_loop *thl = obj;

	if (ctx->found)
		return;
	for (uint64_t i = 0; i < thl->max; ++i) {
		if (thl->loop[i] == ctx->loop) {
			ctx->found = &thl->slot[i];
			return;
		}
	}
}

static threaded_loop_slot *threaded_loop_slot_by_loop(uv_loop_t *loop)
{
	threaded_loop_slot_lookup ctx = { .loop = loop, .found = NULL };

	if (!loop || !ac || !ac->threads)
		return NULL;
	alligator_ht_foreach_arg(ac->threads, threaded_loop_slot_lookup_cb, &ctx);
	return ctx.found;
}

static void threaded_loop_async_cb(uv_async_t *handle)
{
	threaded_loop_slot *slot = handle->data;

	for (;;) {
		threaded_loop_work *w = NULL;

		uv_mutex_lock(&slot->work_lock);
		if (!tommy_list_empty(&slot->work_list)) {
			tommy_node *hn = tommy_list_head(&slot->work_list);
			w = (threaded_loop_work *)hn->data;
			tommy_list_remove_existing(&slot->work_list, hn);
		}
		uv_mutex_unlock(&slot->work_lock);

		if (!w)
			break;
		if (w->fn)
			w->fn(w->arg);
		free(w);
	}
}

static void threaded_loop_stop_cb(void *arg)
{
	uv_loop_t *loop = arg;
	if (loop)
		uv_stop(loop);
}

int threaded_loop_compare(const void *arg, const void *obj)
{
	char *s1 = (char *)arg;
	char *s2 = ((threaded_loop *)obj)->key;
	return strcmp(s1, s2);
}

void threaded_loop_null_timer(uv_timer_t *timer)
{
	(void)timer;
}

void threaded_loop_run(void *arg)
{
	threaded_loop_slot *slot = arg;
	uv_loop_t *loop = slot->loop;

	if (uv_loop_init(loop)) {
		glog(L_ERROR, "threaded_loop: uv_loop_init failed\n");
		return;
	}
	if (uv_mutex_init(&slot->work_lock)) {
		glog(L_ERROR, "threaded_loop: uv_mutex_init failed\n");
		uv_loop_close(loop);
		return;
	}
	slot->work_lock_ready = 1;
	tommy_list_init(&slot->work_list);
	if (uv_async_init(loop, &slot->async, threaded_loop_async_cb)) {
		glog(L_ERROR, "threaded_loop: uv_async_init failed\n");
		uv_mutex_destroy(&slot->work_lock);
		slot->work_lock_ready = 0;
		uv_loop_close(loop);
		return;
	}
	slot->async.data = slot;
	slot->work_lock_ready = 2;

	uv_timer_t timer;
	uv_timer_init(loop, &timer);
	uv_timer_start(&timer, threaded_loop_null_timer, 0, 1000);
	glog(L_INFO, "\tinitialize loop %p thread %lu\n", (void *)loop, (unsigned long)pthread_self());
	uv_run(loop, UV_RUN_DEFAULT);
}

threaded_loop *create_threaded_loop(char *key, uint64_t max)
{
	glog(L_INFO, "initialize threaded_loop '%s' with maximum size %" PRIu64 "\n", key, max);
	threaded_loop *thl = calloc(1, sizeof(*thl));
	thl->max = max;
	thl->key = strdup(key);
	thl->loop = calloc(max, sizeof(thl->loop[0]));
	thl->slot = calloc(max, sizeof(*thl->slot));
	thl->threads = calloc(max, sizeof(*thl->threads));

	for (uint64_t i = 0; i < max; ++i) {
		thl->loop[i] = calloc(1, sizeof(uv_loop_t));
		thl->slot[i].loop = thl->loop[i];
		uv_thread_create(&thl->threads[i], threaded_loop_run, &thl->slot[i]);
	}

	/* Ensure each worker has initialized its async+mutex before we allow queue_work(). */
	for (uint64_t i = 0; i < max; ++i) {
		uint64_t spins = 0;
		while (thl->slot[i].work_lock_ready != 2) {
			usleep(1000);
			if (++spins > 10000) { /* ~10s */
				glog(L_ERROR, "threaded_loop: worker slot %"PRIu64" init timeout\n", i);
				break;
			}
		}
	}

	return thl;
}

int del_threaded_loop(threaded_loop *thl)
{
	if (!thl)
		return 0;

	for (uint64_t i = 0; i < thl->max; ++i)
		threaded_loop_slot_enqueue(&thl->slot[i], threaded_loop_stop_cb, thl->loop[i]);

	for (uint64_t i = 0; i < thl->max; ++i) {
		uv_thread_join(&thl->threads[i]);
		if (thl->slot[i].work_lock_ready) {
			while (!tommy_list_empty(&thl->slot[i].work_list)) {
				tommy_node *hn = tommy_list_head(&thl->slot[i].work_list);
				threaded_loop_work *w = (threaded_loop_work *)hn->data;
				tommy_list_remove_existing(&thl->slot[i].work_list, hn);
				free(w);
			}
			uv_mutex_destroy(&thl->slot[i].work_lock);
			thl->slot[i].work_lock_ready = 0;
		}
		free(thl->loop[i]);
	}

	free(thl->threads);
	free(thl->slot);
	free(thl->loop);
	free(thl->key);
	free(thl);

	return 1;
}

void thread_loop_set(char *key, uint64_t size)
{
	uint32_t hash = tommy_strhash_u32(0, key);
	threaded_loop *thl = alligator_ht_search(ac->threads, threaded_loop_compare, key, hash);

	if (thl) {
		if (thl->max == size)
			return;

		del_threaded_loop(thl);
		alligator_ht_remove_existing(ac->threads, &(thl->node));

		if (size == 0)
			return;
	}

	if (size != 0) {
		thl = create_threaded_loop(key, size);
		alligator_ht_insert(ac->threads, &(thl->node), thl, hash);
	}
}

threaded_loop *get_threaded_loop(char *key)
{
	if (!key)
		return NULL;

	uint32_t hash = tommy_strhash_u32(0, key);
	return alligator_ht_search(ac->threads, threaded_loop_compare, key, hash);
}

uv_loop_t *get_threaded_loop_t(char *key)
{
	threaded_loop *thl = get_threaded_loop(key);
	if (!thl)
		return NULL;

	if (thl->cur == thl->max)
		thl->cur = 0;

	return thl->loop[thl->cur++];
}

uv_loop_t *get_threaded_loop_t_or_default(char *key)
{
	uv_loop_t *loop = get_threaded_loop_t(key);
	if (!loop)
		return uv_default_loop();

	return loop;
}

uv_loop_t *threaded_loop_pin_loop(threaded_loop *thl, const char *pin_key)
{
	uint32_t h;

	if (!thl || thl->max == 0 || !thl->loop)
		return NULL;
	h = pin_key ? tommy_strhash_u32(0, (char *)pin_key) : 0;
	return thl->loop[h % thl->max];
}

int threaded_loop_queue_work(uv_loop_t *loop, void (*fn)(void *), void *arg)
{
	threaded_loop_slot *slot;
	threaded_loop_work *w;

	if (!fn)
		return -1;
	slot = threaded_loop_slot_by_loop(loop);
	if (!slot)
		return -2;
	if (slot->work_lock_ready != 2)
		return -4;
	w = malloc(sizeof(*w));
	if (!w)
		return -3;
	w->fn = fn;
	w->arg = arg;
	uv_mutex_lock(&slot->work_lock);
	tommy_list_insert_tail(&slot->work_list, &w->node, w);
	uv_mutex_unlock(&slot->work_lock);
	uv_async_send(&slot->async);
	return 0;
}

int threaded_loop_is_worker(uv_loop_t *loop)
{
	return threaded_loop_slot_by_loop(loop) != NULL;
}

void thread_loop_foreach(void *arg)
{
	threaded_loop *thl = arg;
	del_threaded_loop(thl);
}

void thread_loop_free(void)
{
	alligator_ht_foreach(ac->threads, thread_loop_foreach);
}

uint64_t get_threads_num(json_t *value)
{
	if (json_typeof(value) == JSON_STRING) {
		const char *data = json_string_value(value);
		if (!strcmp(data, "auto")) {
			return (uint64_t)sysconf(_SC_NPROCESSORS_ONLN);
		}
		return strtoull(data, NULL, 10);
	}
	return (uint64_t)json_integer_value(value);
}
