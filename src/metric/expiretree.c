#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <ctype.h>
#include "expiretree.h"
#include "common/logs.h"
#include "common/rtime.h"
#include "main.h"

#define u64	PRIu64

/*
 * Level 0: 256 slots of 1 second.
 * Level 1: 64 slots of 256 seconds   (~4.5 h).
 * Level 2: 64 slots of 16384 seconds (~4.5 h * 64, ~12 days).
 * Level 3: 64 slots of that span     (~2 years).
 * Beyond that, and "never expire" keys, sit on `far` and are not scanned
 * on the normal purge path.
 */
#define EXPIRE_SPAN0 256
#define EXPIRE_SPAN1 (EXPIRE_SPAN0 * EXPIRE_LVN_SIZE)
#define EXPIRE_SPAN2 (EXPIRE_SPAN1 * EXPIRE_LVN_SIZE)
#define EXPIRE_SPAN3 (EXPIRE_SPAN2 * EXPIRE_LVN_SIZE)

#define EXPIRE_LV_DUE 0
#define EXPIRE_LV0    1
#define EXPIRE_LV1    2
#define EXPIRE_LV2    3
#define EXPIRE_LV3    4
#define EXPIRE_LV_FAR 5

static expire_node **expire_bucket(expire_tree *tree, expire_node *n)
{
	switch (n->level) {
	case EXPIRE_LV_DUE: return &tree->due;
	case EXPIRE_LV0:    return &tree->lv0[n->slot];
	case EXPIRE_LV1:    return &tree->lv1[n->slot];
	case EXPIRE_LV2:    return &tree->lv2[n->slot];
	case EXPIRE_LV3:    return &tree->lv3[n->slot];
	default:            return &tree->far;
	}
}

static void expire_push(expire_node **head, expire_node *n)
{
	n->prev = NULL;
	n->next = *head;
	if (*head)
		(*head)->prev = n;
	*head = n;
}

/* Safe if the node was already unlinked. */
static void expire_unlink(expire_tree *tree, expire_node *n)
{
	expire_node **head = expire_bucket(tree, n);
	if (!head)
		return;
	if (n->prev)
		n->prev->next = n->next;
	else if (*head == n)
		*head = n->next;
	else
		return;
	if (n->next)
		n->next->prev = n->prev;
	n->next = NULL;
	n->prev = NULL;
}

static int64_t expire_base(expire_tree *tree)
{
	if (tree->cursor > 0)
		return tree->cursor;
	r_time t = setrtime();
	return t.sec;
}

static void expire_schedule(expire_tree *tree, expire_node *n)
{
	int64_t delta = n->key - expire_base(tree);
	n->next = NULL;
	n->prev = NULL;
	if (delta <= 0) {
		n->level = EXPIRE_LV_DUE;
		n->slot = 0;
		expire_push(&tree->due, n);
		return;
	}
	if (delta < EXPIRE_SPAN0) {
		n->level = EXPIRE_LV0;
		n->slot = (uint16_t)(n->key & (EXPIRE_LV0_SIZE - 1));
		expire_push(&tree->lv0[n->slot], n);
		return;
	}
	if (delta < EXPIRE_SPAN1) {
		n->level = EXPIRE_LV1;
		n->slot = (uint16_t)((n->key / EXPIRE_SPAN0) & (EXPIRE_LVN_SIZE - 1));
		expire_push(&tree->lv1[n->slot], n);
		return;
	}
	if (delta < EXPIRE_SPAN2) {
		n->level = EXPIRE_LV2;
		n->slot = (uint16_t)((n->key / EXPIRE_SPAN1) & (EXPIRE_LVN_SIZE - 1));
		expire_push(&tree->lv2[n->slot], n);
		return;
	}
	if (delta < EXPIRE_SPAN3) {
		n->level = EXPIRE_LV3;
		n->slot = (uint16_t)((n->key / EXPIRE_SPAN2) & (EXPIRE_LVN_SIZE - 1));
		expire_push(&tree->lv3[n->slot], n);
		return;
	}
	n->level = EXPIRE_LV_FAR;
	n->slot = 0;
	expire_push(&tree->far, n);
}

static void expire_cascade(expire_tree *tree, expire_node **head)
{
	expire_node *n = *head;
	*head = NULL;
	while (n) {
		expire_node *next = n->next;
		expire_schedule(tree, n);
		n = next;
	}
}

static void expire_take_bucket(expire_node **head, expire_node **acc)
{
	expire_node *n = *head;
	*head = NULL;
	while (n) {
		expire_node *next = n->next;
		n->next = *acc;
		n->prev = NULL;
		if (*acc)
			(*acc)->prev = n;
		*acc = n;
		n = next;
	}
}

static void expire_reschedule_all(expire_tree *tree)
{
	expire_node *acc = NULL;
	uint64_t i;
	expire_take_bucket(&tree->due, &acc);
	expire_take_bucket(&tree->far, &acc);
	for (i = 0; i < EXPIRE_LV0_SIZE; i++)
		expire_take_bucket(&tree->lv0[i], &acc);
	for (i = 0; i < EXPIRE_LVN_SIZE; i++) {
		expire_take_bucket(&tree->lv1[i], &acc);
		expire_take_bucket(&tree->lv2[i], &acc);
		expire_take_bucket(&tree->lv3[i], &acc);
	}
	while (acc) {
		expire_node *next = acc->next;
		expire_schedule(tree, acc);
		acc = next;
	}
}

/* Move the cursor forward to `now`, cascading higher levels as their slots open. */
static void expire_wheel_advance(expire_tree *tree, int64_t now)
{
	if (tree->cursor <= 0) {
		tree->cursor = now;
		expire_reschedule_all(tree);
		return;
	}
	if (now < tree->cursor)
		return;
	if (now - tree->cursor > EXPIRE_SPAN1) {
		tree->cursor = now;
		expire_reschedule_all(tree);
		return;
	}
	while (tree->cursor < now) {
		tree->cursor++;
		if ((tree->cursor & (EXPIRE_SPAN0 - 1)) == 0)
			expire_cascade(tree, &tree->lv1[(tree->cursor / EXPIRE_SPAN0) & (EXPIRE_LVN_SIZE - 1)]);
		if (tree->cursor % EXPIRE_SPAN1 == 0)
			expire_cascade(tree, &tree->lv2[(tree->cursor / EXPIRE_SPAN1) & (EXPIRE_LVN_SIZE - 1)]);
		if (tree->cursor % EXPIRE_SPAN2 == 0)
			expire_cascade(tree, &tree->lv3[(tree->cursor / EXPIRE_SPAN2) & (EXPIRE_LVN_SIZE - 1)]);

		expire_node *slot = tree->lv0[tree->cursor & (EXPIRE_LV0_SIZE - 1)];
		tree->lv0[tree->cursor & (EXPIRE_LV0_SIZE - 1)] = NULL;
		while (slot) {
			expire_node *next = slot->next;
			if (slot->key <= now)
			{
				slot->level = EXPIRE_LV_DUE;
				slot->slot = 0;
				expire_push(&tree->due, slot);
			}
			else
				expire_schedule(tree, slot);
			slot = next;
		}
	}
}

static void expire_drain_all(expire_tree *tree)
{
	expire_node *acc = NULL;
	uint64_t i;
	expire_take_bucket(&tree->due, &acc);
	expire_take_bucket(&tree->far, &acc);
	for (i = 0; i < EXPIRE_LV0_SIZE; i++)
		expire_take_bucket(&tree->lv0[i], &acc);
	for (i = 0; i < EXPIRE_LVN_SIZE; i++) {
		expire_take_bucket(&tree->lv1[i], &acc);
		expire_take_bucket(&tree->lv2[i], &acc);
		expire_take_bucket(&tree->lv3[i], &acc);
	}
	tree->due = NULL;
	while (acc) {
		expire_node *next = acc->next;
		acc->level = EXPIRE_LV_DUE;
		acc->slot = 0;
		acc->prev = NULL;
		acc->next = tree->due;
		if (tree->due)
			tree->due->prev = acc;
		tree->due = acc;
		acc = next;
	}
}

void expire_arm_locked ( expire_tree *tree, int64_t key, metric_node *metric )
{
	expire_node *n;
	if (!tree || !metric)
		return;
	if (metric->expire_node)
		expire_forget_locked(tree, metric);
	n = calloc(1, sizeof(*n));
	if (!n)
		return;
	n->key = key;
	n->metric = metric;
	metric->expire_node = n;
	tree->count++;
	expire_schedule(tree, n);
}

void expire_arm ( expire_tree *tree, int64_t key, metric_node *metric )
{
	if (!tree || !metric)
		return;
	pthread_rwlock_wrlock(tree->rwlock);
	expire_arm_locked(tree, key, metric);
	pthread_rwlock_unlock(tree->rwlock);
}

void expire_forget_locked ( expire_tree *tree, metric_node *metric )
{
	expire_node *n;
	if (!tree || !metric || !metric->expire_node)
		return;
	n = metric->expire_node;
	expire_unlink(tree, n);
	metric->expire_node = NULL;
	free(n);
	if (tree->count > 0)
		tree->count--;
}

void expire_forget ( expire_tree *tree, metric_node *metric )
{
	if (!tree || !metric)
		return;
	pthread_rwlock_wrlock(tree->rwlock);
	expire_forget_locked(tree, metric);
	pthread_rwlock_unlock(tree->rwlock);
}

static uint64_t expire_count_list(expire_node *n, int64_t key)
{
	uint64_t c = 0;
	for (; n; n = n->next)
		if (n->key <= key)
			c++;
	return c;
}

uint64_t expire_count_expired ( expire_tree *tree, int64_t key )
{
	uint64_t c = 0;
	uint64_t i;
	int locked;
	if (!tree)
		return 0;
	locked = 1;
	pthread_rwlock_rdlock(tree->rwlock);
	c += expire_count_list(tree->due, key);
	c += expire_count_list(tree->far, key);
	for (i = 0; i < EXPIRE_LV0_SIZE; i++)
		c += expire_count_list(tree->lv0[i], key);
	for (i = 0; i < EXPIRE_LVN_SIZE; i++) {
		c += expire_count_list(tree->lv1[i], key);
		c += expire_count_list(tree->lv2[i], key);
		c += expire_count_list(tree->lv3[i], key);
	}
	if (locked)
		pthread_rwlock_unlock(tree->rwlock);
	return c;
}

static uint64_t expire_backptr_list(expire_node *n)
{
	uint64_t c = 0;
	for (; n; n = n->next)
		if (!n->metric || n->metric->expire_node != n)
			c++;
	return c;
}

uint64_t expire_backptr_violations ( expire_tree *tree )
{
	uint64_t c = 0;
	uint64_t i;
	if (!tree)
		return 0;
	pthread_rwlock_rdlock(tree->rwlock);
	c += expire_backptr_list(tree->due);
	c += expire_backptr_list(tree->far);
	for (i = 0; i < EXPIRE_LV0_SIZE; i++)
		c += expire_backptr_list(tree->lv0[i]);
	for (i = 0; i < EXPIRE_LVN_SIZE; i++) {
		c += expire_backptr_list(tree->lv1[i]);
		c += expire_backptr_list(tree->lv2[i]);
		c += expire_backptr_list(tree->lv3[i]);
	}
	pthread_rwlock_unlock(tree->rwlock);
	return c;
}

static void expire_show_list(expire_node *n, const char *where)
{
	for (; n; n = n->next)
		glog(L_TRACE, "expire %s key=%"u64" metric=%p\n", where, n->key, n->metric);
}

void expire_show ( expire_tree *tree )
{
	uint64_t i;
	if (!tree)
		return;
	pthread_rwlock_rdlock(tree->rwlock);
	expire_show_list(tree->due, "due");
	expire_show_list(tree->far, "far");
	for (i = 0; i < EXPIRE_LV0_SIZE; i++)
		expire_show_list(tree->lv0[i], "lv0");
	pthread_rwlock_unlock(tree->rwlock);
}

void expire_free ( expire_tree *tree )
{
	if (!tree)
		return;
	pthread_rwlock_wrlock(tree->rwlock);
	expire_drain_all(tree);
	while (tree->due) {
		expire_node *n = tree->due;
		expire_unlink(tree, n);
		if (n->metric)
			n->metric->expire_node = NULL;
		free(n);
	}
	tree->count = 0;
	pthread_rwlock_unlock(tree->rwlock);
}

void expire_build (char *namespace)
{
	extern aconf *ac;
	(void)namespace;
	if (ac && ac->nsdefault && ac->nsdefault->expiretree)
		expire_show(ac->nsdefault->expiretree);
}

void expire_purge(uint64_t key, char *namespace, namespace_struct *ns)
{
	extern aconf *ac;
	metric_tree *tree;
	expire_tree *expiretree;
	int drain_all;
	int advanced;
	int64_t guard;
	uint64_t orphans = 0;
	r_time start, end;

	glog(L_INFO, "run expire purge on namespace %s/%s\n", namespace, ns->key);

	if (!ns) {
		if (!namespace)
			ns = ac->nsdefault;
		else
			ns = get_namespace(namespace);
	}
	if (!ns || !ns->metrictree || !ns->expiretree)
		return;

	tree = ns->metrictree;
	expiretree = ns->expiretree;
	drain_all = (key == (uint64_t)INT64_MAX);
	advanced = 0;

	/* Lock order everywhere is metric tree then expire tree. */
	pthread_rwlock_wrlock(tree->rwlock);
	pthread_rwlock_wrlock(expiretree->rwlock);

	start = setrtime();
	/*
	 * Deleting one series can move another series' sample into the surviving
	 * red-black node and re-arm it. A full wipe has to collect again, or those
	 * re-armed series would outlive the purge. A timed purge re-arms only a
	 * series that was not due, so one pass is enough.
	 */
	guard = tree->count + expiretree->count + 1;
	do {
		if (drain_all)
			expire_drain_all(expiretree);
		else if (!advanced) {
			expire_wheel_advance(expiretree, (int64_t)key);
			advanced = 1;
		}

		while (expiretree->due && guard-- > 0)
		{
			expire_node *n = expiretree->due;
			metric_node *m;
			if (!drain_all && n->key > (int64_t)key)
			{
				expire_unlink(expiretree, n);
				expire_schedule(expiretree, n);
				continue;
			}
			m = n->metric;
			if (m && m->labels && metric_delete_locked(tree, m->labels, expiretree))
				continue;
			/* The metric tree no longer contains this series. Drop the expire
			   entry so the loop cannot spin on it, and count it as corruption. */
			if (m && m->expire_node)
				expire_forget_locked(expiretree, m);
			else if (expiretree->due == n)
			{
				expire_unlink(expiretree, n);
				free(n);
			}
			orphans++;
		}
	} while (drain_all && tree->count > 0 && guard > 0);

	pthread_rwlock_unlock(expiretree->rwlock);
	pthread_rwlock_unlock(tree->rwlock);

	end = setrtime();
	{
		uint64_t expire_time = getrtime_mcs(start, end, 0);
		if (ac->nsdefault && ac->nsdefault->metrictree && ac->nsdefault->metrictree->count)
			metric_update("alligator_gc_duration_microseconds", NULL, &expire_time, DATATYPE_UINT, NULL);
	}

	/* Only on corruption: expire_purge also runs from namespace_free, where
	   emitting into another namespace's tree during teardown is unsafe. */
	if (orphans)
	{
		glog(L_ERROR, "expire purge on namespace '%s' left %"u64" expired entries whose metric could not be reached\n", ns->key, orphans);
		metric_add_labels("alligator_expire_orphan_entries", &orphans, DATATYPE_UINT, NULL, "namespace", ns->key);
	}
}
