#pragma once
#include "dstructures/tommy.h"

typedef struct ngram_node_t {
	tommy_hashdyn_node node;
	char *ngram;
	uint64_t count;
	void **data;
} ngram_node_t;

typedef struct filter_node_t {
	tommy_hashdyn_node node;
	uint64_t key;
} filter_node_t;

typedef struct ngram_index_t {
	alligator_ht hash;
	uint64_t parts_in_node;
	alligator_ht deleted;
	void (*data_clear_func)(void*);
} ngram_index_t;

ngram_index_t *ngram_index_init(uint64_t parts_in_node);
void ngram_filter_free(ngram_node_t *filtered_results);
void ngram_clear(ngram_index_t *ngram_table, void *clear_func);
ngram_node_t* ngram_filter(ngram_node_t **results, uint64_t size);
uint64_t ngram_token_search(ngram_index_t *ngram_table, ngram_node_t ***ret, uint8_t ngram_size, const char* token);
uint64_t ngram_token_add(ngram_index_t *ngram_table, uint8_t ngram_size, const char *token, void *data);
