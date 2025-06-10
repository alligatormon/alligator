#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dstructures/tommy.h"
#include "dstructures/ngram/ngram.h"

static inline int ngram_cmp(const void* arg, const void* obj) {
	const char* key = (const char*)arg;
	const ngram_node_t* node = (const ngram_node_t*)obj;
	return strcmp(key, node->ngram);
}

static inline int filter_node_compare(const void* arg, const void* obj) {
	uint64_t key = (const uint64_t)arg;
	const filter_node_t* node = (const filter_node_t*)obj;
	return key != node->key;
}

uint64_t ngram_add(ngram_index_t *ngram_table, const char* ngram, void *data) {
	alligator_ht* hash = &ngram_table->hash;
	unsigned int hash_val = tommy_strhash_u32(0, ngram);

	ngram_node_t* node = alligator_ht_search(hash, ngram_cmp, (void*)ngram, hash_val);
	if (node) {
		if (node->count == ngram_table->parts_in_node) {
			fprintf(stderr, "ngram_add error: too much parts per node: '%s'\n", ngram);
			return 0;
		}
		uint64_t prev_count = node->count == 0 ? 0 : node->count - 1;
		if (node->data[prev_count] == data) {
			return 0;
		}
		node->data[node->count] = data;
		node->count++;
	} else {
		node = calloc(1, sizeof(ngram_node_t));
		node->ngram = strdup(ngram);
		node->data = calloc(1, ngram_table->parts_in_node * sizeof(void*));
		node->data[node->count] = data;
		node->count = 1;
		alligator_ht_insert(hash, &node->node, node, hash_val);
	}

	return node->count;
}

uint64_t ngram_token_add(ngram_index_t *ngram_table, uint8_t ngram_size, const char *token, void *data) {
	uint64_t len = strlen(token);
	if (len < ngram_size) {
		return 0;
	}

	uint64_t results = 0;
	for (uint64_t i = 0; i < len - ngram_size; ++i) {
		char buf[ngram_size];
		memcpy(buf, token + i, ngram_size);
		buf[ngram_size] = 0;
		results += ngram_add(ngram_table, buf, data);
	}

	return results;
}

int ngram_get_count(ngram_index_t *ngram_table, const char* ngram) {
	alligator_ht* hash = &ngram_table->hash;
	unsigned int hash_val = tommy_strhash_u32(0, ngram);

	ngram_node_t* node = alligator_ht_search(hash, ngram_cmp, (void*)ngram, hash_val);
	return node ? node->count : 0;
}

ngram_node_t *ngram_search(ngram_index_t *ngram_table, const char* ngram) {
	alligator_ht* hash = &ngram_table->hash;
	unsigned int sum = tommy_strhash_u32(0, ngram);
	ngram_node_t *entry = alligator_ht_search(hash, ngram_cmp, ngram, sum);
	return entry;
}

uint64_t ngram_token_search(ngram_index_t *ngram_table, ngram_node_t ***ret, uint8_t ngram_size, const char* token) {
	uint64_t len = strlen(token);
	if (len < ngram_size) {
		return 0;
	}

	uint64_t loop_for = len - ngram_size;
	ngram_node_t **results = calloc(1, loop_for * sizeof(*results));
	uint64_t j = 0;

	for (uint64_t i = 0; i < loop_for; ++i) {
		char buf[ngram_size];
		memcpy(buf, token + i, ngram_size);
		buf[ngram_size] = 0;
		ngram_node_t *result = ngram_search(ngram_table, buf);
		if (result)
			results[j++] = result;
	}

	*ret = results;
	return j;
}

ngram_node_t* ngram_filter(ngram_node_t **results, uint64_t size) {
	alligator_ht *filter = alligator_ht_init(NULL);

	uint64_t sum = 0;
	for (uint64_t i = 0; i < size; ++i) {
		sum += results[i]->count;
	}

	ngram_node_t *ret = calloc(1, sizeof(*ret));
	ret->data = calloc(1, sum * sizeof(void*));

	uint64_t k = 0;
	for (uint64_t i = 0; i < size; ++i) {
		for (uint64_t j = 0; j < results[i]->count; ++j) {
			uint64_t addr = (uint64_t)results[i]->data[j];
			filter_node_t *fn = alligator_ht_search(filter, filter_node_compare, (void*)addr, addr);
			if (!fn) {
				filter_node_t *fn = calloc(1, sizeof(*fn));
				fn->key = addr;
				alligator_ht_insert(filter, &fn->node, fn, addr);
				ret->data[k++] = results[i]->data[j];
			}
		}
	}

	alligator_ht_forfree(filter, free);
    free(filter);
	ret->count = k;
	return ret;
}

void ngram_free(void *arg, ngram_index_t *ngram_table) {
	ngram_node_t* node = arg;

	for (uint64_t i = 0; i < node->count; ++i) {
		uint64_t addr = (uint64_t)node->data[i];
		filter_node_t *fn = alligator_ht_search(&ngram_table->deleted, filter_node_compare, (void*)addr, addr);
		if (!fn) {
			filter_node_t *fn = calloc(1, sizeof(*fn));
			fn->key = addr;
			alligator_ht_insert(&ngram_table->deleted, &fn->node, fn, addr);
			if (ngram_table->data_clear_func)
				ngram_table->data_clear_func(node->data[i]);
		}
	}

	free(node->ngram);
	if (node->data)
		free(node->data);
	free(node);
}

void ngram_hash_free_node(void *funcfree, void* arg)
{
	ngram_index_t *ngram_table = funcfree;
	ngram_free(arg, ngram_table);
}

void ngram_clear(ngram_index_t *ngram_table, void *clear_func) {
	alligator_ht* hash = &ngram_table->hash;
	ngram_table->data_clear_func = clear_func;
	alligator_ht_init(&ngram_table->deleted);

	alligator_ht_foreach_arg(hash, ngram_hash_free_node, ngram_table);
	alligator_ht_done(hash);

	alligator_ht_forfree(&ngram_table->deleted, free);
	alligator_ht_done(&ngram_table->deleted);
	free(ngram_table);
}

void ngram_filter_free(ngram_node_t *filtered_results) {
	free(filtered_results->data);
	free(filtered_results);
}

ngram_index_t *ngram_index_init(uint64_t parts_in_node) {
	ngram_index_t *ngram_table = calloc(1, sizeof(*ngram_table));
	ngram_table->parts_in_node = parts_in_node;
	alligator_ht_init(&ngram_table->hash);

	return ngram_table;
}
