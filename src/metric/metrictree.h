#pragma once
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <ctype.h>
#include "metric/labels.h"
#include "metric/percentile_heap.h"
#include "common/selector.h"

#define	RED	1
#define	BLACK	0
#define	RIGHT	1
#define	LEFT	0

typedef struct sort_order_node {
	char *name;
	tommy_node node;
	uint32_t hash;
} sort_order_node;

typedef struct sortplan
{
	char *plan[65535];
	size_t size;
} sortplan;

typedef struct labels_t
{
	char *name;
	size_t name_len;
	char *key;
	size_t key_len;
	sortplan *sort_plan;
	uint8_t allocatedname;
	uint8_t allocatedkey;

	struct labels_t *next;
} labels_t;

typedef struct labels_container
{
	char *name;
	char *key;
	uint8_t allocatedname;
	uint8_t allocatedkey;

	tommy_node node;
} labels_container;

typedef struct labels_words_cache
{
	char *w;
	size_t l;
	size_t count;

	tommy_node node;
} labels_words_cache;

typedef struct metric_list {
	union
	{
		double d;
		uint64_t u;
		int64_t i;
		char *s; // string
	};
	uint64_t timestamp;
	uint64_t serial;
} metric_list;

struct expire_node;
struct expire_tree;

typedef struct metric_node 
{
	union
	{
		double d;
		uint64_t u;
		int64_t i;
		char *s; // string
		char *index; // index file
		metric_list *list;
	};
	int16_t index_element_list;
	int8_t type;
	percentile_buffer *pb;

	int8_t en;
	struct metric_node *steam[2];
	//struct metric_tree *stree;
	labels_t *labels;
	int color;
	struct expire_node *expire_node;
} metric_node;

typedef struct expire_node 
{
	int color;
	int64_t key;
	metric_node *metric;
	struct expire_node *steam[2];
} expire_node;

typedef struct metric_tree 
{
	struct metric_node *root;
	int64_t count;
	size_t str_size;
	sortplan *sort_plan;
	alligator_ht* labels_words_hash;
} metric_tree;

typedef struct mapping_label
{
	char *name;
	char *key;
	struct mapping_label *next;
} mapping_label;

typedef struct mapping_metric
{
	char *metric_name;
	char *template;
	size_t template_len;
	struct mapping_metric *next;
	uint64_t match;
	size_t glob_size;
	char **glob;

	int64_t *percentile;
	int64_t percentile_size;
	double *bucket;
	int64_t bucket_size;
	double *le;
	int64_t le_size;

	size_t wildcard;
	mapping_label *label_head;
	mapping_label *label_tail;
} mapping_metric;

typedef struct query_struct {
	char *metric_name;
	char *key;
	double val;
	double min;
	double max;
	alligator_ht *lbl;
	uint64_t count;
	tommy_node node;
} query_struct;


//void metric_add_labels5(char *name, void* value, int8_t type, char *namespace, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5);
int metric_delete (metric_tree *tree, labels_t *labels, struct expire_tree *expiretree);
metric_node* metric_find(metric_tree *tree, labels_t* labels);
int64_t metric_get_double(void *value, int8_t type);
void metrictree_get(metric_node *x, labels_t* labels, string *str);
void labels_gen_string(labels_t *labels, int l, string *str, metric_node *x);
void labels_free(labels_t *labels, metric_tree *metrictree);
void metric_build (char *namespace, string *s);

void labels_cache_fill(labels_t *labels, metric_tree *metrictree);
int labels_cmp(sortplan *sort_plan, labels_t *labels1, labels_t *labels2);
void labels_print(labels_t *labels, int l);
void labels_cat(labels_t *labels, int l, string *s, int64_t ttl, int color);
int labels_match(sortplan* sort_plan, labels_t *labels1, labels_t *labels2, size_t labels_count);
void labels_gen_metric(labels_t *labels_list, int l, metric_node *x, string *groupkey, alligator_ht *res_hash);
int metric_name_match(labels_t *labels1, labels_t *labels2);
void labels_head_free(labels_t *labels);
