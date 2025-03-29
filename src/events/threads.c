#include <common/logs.h>
#include <events/context_arg.h>
#include <main.h>
extern aconf *ac;

int threaded_loop_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((threaded_loop*)obj)->key;
	return strcmp(s1, s2);
}
void threaded_loop_null_timer(uv_timer_t *timer) {
}

void threaded_loop_run(void *arg) {
	uv_loop_t *loop = arg;
	uv_loop_init(loop);
	uv_timer_t timer;
	uv_timer_init(loop, &timer);
	uv_timer_start(&timer, threaded_loop_null_timer, 0, 1000);
	glog(L_INFO, "\tinitialize loop %p thread %d\n", loop, pthread_self());
	uv_run(loop, UV_RUN_DEFAULT);
}

threaded_loop* create_threaded_loop(char *key, uint64_t max) {
	glog(L_INFO, "initialize threaded_loop '%s' with maximum size %"PRIu64"\n", key, max);
	threaded_loop *thl = calloc(1, sizeof(*thl));
	thl->max = max;
	thl->key = strdup(key);
	thl->loop = calloc(1, sizeof(void*) * max);

	for (uint64_t i = 0; i < max; ++i) {
		thl->loop[i] = calloc(1, sizeof(uv_loop_t));
		uv_thread_create(&thl->thread, threaded_loop_run, thl->loop[i]);
	}

	return thl;
}

int del_threaded_loop(threaded_loop *thl) {

	for (uint64_t i = 0; i < thl->max; ++i) {
		uv_stop(thl->loop[i]);
		free(thl->loop[i]);
	}

	free(thl->loop);
	free(thl->key);
	free(thl);

	return 1;
}

void resize_threaded_loop(threaded_loop *thl, uint64_t newsize) {
	thl->max = newsize;

	if (newsize > thl->max) {
		for (uint64_t i = 0; i < thl->max; ++i) {
			uv_stop(thl->loop[i]);
			free(thl->loop[i]);
		}
		free(thl->loop);

		thl->loop = calloc(1, sizeof(uv_loop_t) * newsize);
		for (uint64_t i = 0; i < newsize; ++i) {
			thl->loop[i] = calloc(1, sizeof(uv_loop_t));
			uv_run(thl->loop[i], UV_RUN_DEFAULT);
		}
		thl->loop = calloc(1, sizeof(uv_loop_t));
	}
	else if (newsize < thl->max) {
		for (uint64_t i = newsize; i < thl->max; ++i) {
			uv_stop(thl->loop[i]);
			free(thl->loop[i]);
		}
	}
}

void thread_loop_set(char *key, uint64_t size) {
	uint32_t hash = tommy_strhash_u32(0, key);
	threaded_loop *thl = alligator_ht_search(ac->threads, threaded_loop_compare, key, hash);
	if (thl) {
		if (thl->max == size)
			return;
		
		if (size != 0)
			resize_threaded_loop(thl, size);
		else
			del_threaded_loop(thl);
	}
	else {
		threaded_loop* thl = create_threaded_loop(key, size);
		alligator_ht_insert(ac->threads, &(thl->node), thl, hash);
	}
}

threaded_loop* get_threaded_loop(char *key) {
	if (!key)
		return NULL;

	uint32_t hash = tommy_strhash_u32(0, key);
	threaded_loop *thl = alligator_ht_search(ac->threads, threaded_loop_compare, key, hash);
	return thl;
}

uv_loop_t *get_threaded_loop_t(char *key) {
	threaded_loop *thl = get_threaded_loop(key);
	if (!thl)
		return NULL;

	if (thl->cur == thl->max)
		thl->cur = 0;

	return thl->loop[thl->cur++];
}

uv_loop_t *get_threaded_loop_t_or_default(char *key) {
	uv_loop_t *loop = get_threaded_loop_t(key);
	if (!loop)
		return uv_default_loop();
	
	return loop;
}

void thread_loop_foreach(void *arg)
{
	threaded_loop *thl = arg;
	del_threaded_loop(thl);
}

void thread_loop_free() {
	alligator_ht_foreach(ac->threads, thread_loop_foreach);
}


uint64_t get_threads_num(json_t *value) {
	if (json_typeof(value) == JSON_STRING) {
		const char *data = json_string_value(value);
		if (!strcmp(data, "auto")) {
			return sysconf(_SC_NPROCESSORS_ONLN);
		}
		else
			return strtoull(data, NULL, 10);
	}
	else {
		return  json_integer_value(value);
	}
}
