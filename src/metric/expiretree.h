#pragma once
#include "metric/metrictree.h"

/*
 * Hierarchical timing wheel. Level 0 is one second per slot. Higher levels
 * hold events further out and cascade down as the cursor reaches them.
 * Events already due sit on `due` so a purge is O(expired), not O(series).
 */
#define EXPIRE_LV0_SIZE 256
#define EXPIRE_LVN_SIZE 64

typedef struct expire_tree 
{
	struct expire_node *due;
	struct expire_node *lv0[EXPIRE_LV0_SIZE];
	struct expire_node *lv1[EXPIRE_LVN_SIZE];
	struct expire_node *lv2[EXPIRE_LVN_SIZE];
	struct expire_node *lv3[EXPIRE_LVN_SIZE];
	struct expire_node *far;
	int64_t cursor;
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
uint64_t expire_count_expired ( expire_tree *tree, int64_t key );
uint64_t expire_backptr_violations ( expire_tree *tree );
