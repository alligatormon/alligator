#pragma once
#include "metric/metrictree.h"

typedef struct expire_tree 
{
	struct expire_node *root;
	int64_t count;
	pthread_rwlock_t *rwlock;
} expire_tree;

metric_node* metric_insert (metric_tree *tree, labels_t *labels, int8_t type, void* value, expire_tree *expiretree, int64_t ttl);
void metric_set(metric_node *mnode, int8_t type, void* value, expire_tree *expiretree, int64_t ttl);

/*
 * Expiry ownership: the only functions that may touch the
 * metric_node <-> expire_node link. `_locked` requires expiretree->rwlock held.
 */
void expire_arm ( expire_tree *tree, int64_t key, metric_node *metric );
void expire_arm_locked ( expire_tree *tree, int64_t key, metric_node *metric );
void expire_forget ( expire_tree *tree, metric_node *metric );
void expire_forget_locked ( expire_tree *tree, metric_node *metric );
uint64_t expire_count_expired ( expire_node *x, int64_t key );
