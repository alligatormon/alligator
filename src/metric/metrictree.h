#pragma once
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <ctype.h>
#include "metric/labels.h"

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

	struct labels_t *next;
} labels_t;

typedef struct labels_container
{
	char *name;
	char *key;

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
	tommy_hashdyn* labels_words_hash;
} metric_tree;


void metric_add_labels5(char *name, void* value, int8_t type, char *namespace, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5);
void metric_delete (metric_tree *tree, labels_t *labels, struct expire_tree *expiretree);
metric_node* metric_find(metric_tree *tree, labels_t* labels);
