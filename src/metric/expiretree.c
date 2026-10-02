#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <ctype.h>
#include "expiretree.h"
#include "common/logs.h"
#include "main.h"

#define u64	PRIu64
#define d64	PRId64
#define	RED	1
#define	BLACK	0
#define	RIGHT	1
#define	LEFT	0

int labels_cmp(sortplan *sort_plan, labels_t *labels1, labels_t *labels2);

static void expire_insert_inner ( expire_tree *tree, int64_t key, metric_node *metric );
static int expire_delete_inner ( expire_tree *tree, int64_t key, metric_node *metric );


int expire_is_red ( expire_node *node )
{
	return node != NULL && node->color == RED;
}

expire_node *expire_single ( expire_node *root, int dir )
{
	expire_node *save = root->child[!dir];
 
	root->child[!dir] = save->child[dir];
	save->child[dir] = root;
 
	root->color = RED;
	save->color = BLACK;
 
	return save;
}
 
expire_node *expire_double ( expire_node *root, int dir )
{
	root->child[!dir] = expire_single ( root->child[!dir], !dir );
	return expire_single ( root, dir );
}

expire_node *expire_make_node ( int64_t key, metric_node *metric )
{
	expire_node *rn = malloc ( sizeof *rn );
  
	if ( rn != NULL ) {
		rn->key = key;
		rn->color = RED;
		rn->child[LEFT] = NULL;
		rn->child[RIGHT] = NULL;
		rn->metric = metric;
		metric->expire_node = rn;
	}
	return rn;
}

static void expire_insert_inner ( expire_tree *tree, int64_t key, metric_node *metric )
{
	if ( tree->root == NULL )
	{
	    tree->root = expire_make_node ( key, metric );
	    if ( tree->root == NULL )
			return;
		tree->count ++ ;
	}
	else
	{
		expire_node *head = calloc(1, sizeof(*head));
		if (!head)
			return;
		expire_node *g, *t;
		expire_node *p, *q;
		int dir = 0, last = 0;

		t = head;
		g = p = NULL;
		q = t->child[RIGHT] = tree->root;

		for (;;) 
		{
			int flag = 0;
			if ( q == NULL )
			{
				flag = 1;
				p->child[dir] = q = expire_make_node ( key, metric );
				tree->count ++ ;
				if ( q == NULL ) {
					free(head);
					return;
				}
			}
			else if ( expire_is_red ( q->child[LEFT] ) && expire_is_red ( q->child[RIGHT] ) ) 
			{
				q->color = RED;
				q->child[LEFT]->color = BLACK;
				q->child[RIGHT]->color = BLACK;
			}

			if ( expire_is_red ( q ) && expire_is_red ( p ) ) 
			{
				int dir2 = t->child[RIGHT] == g;
				if ( q == p->child[last] )
					t->child[dir2] = expire_single ( g, !last );
				else
					t->child[dir2] = expire_double ( g, !last );
			}

			if (flag)
			{
				break;
			}

			last = dir;
			if (q->key == key)
				dir = q->metric <= metric;
			else
				dir = q->key <= key;

			if ( g != NULL )
				t = g;
			g = p, p = q;
			q = q->child[dir];
		}
		tree->root = head->child[RIGHT];
		free(head);
	}
	tree->root->color = BLACK;
}

void expire_build (char *namespace);

static int expire_delete_inner ( expire_tree *tree, int64_t key, metric_node *metric )
{
	int excode = 0;
	if ( tree->root != NULL ) 
	{
		expire_node *head = calloc(1, sizeof(*head));
		if (!head)
			return 0;
		expire_node *q, *p, *g;
		expire_node *f = NULL;
		int dir = 1;
 
		q = head;
		g = p = NULL;
		q->child[RIGHT] = tree->root;
		int last = dir;

		while ( q->child[dir] != NULL )
		{
			last = dir;
 
			g = p, p = q;
			q = q->child[dir];

			if (metric && q->key == key)
			{
				dir = q->metric <= metric;
			}
			else
			{
				dir = q->key <= key;
			}

			if (!metric && q->key <= key)
			{
				f = q;
			}
			else if ( q->metric == metric )
			{
				f = q;
			}
 
			if ( !expire_is_red ( q ) && !expire_is_red ( q->child[dir] ) )
			{
				if ( expire_is_red ( q->child[!dir] ) )
					p = p->child[last] = expire_single ( q, dir );
				else if ( !expire_is_red ( q->child[!dir] ) )
				{
					expire_node *s = p->child[!last];

					if ( s != NULL )
					{
						if ( !expire_is_red ( s->child[!last] ) && !expire_is_red ( s->child[last] ) )
						{
							p->color = BLACK;
							s->color = RED;
							q->color = RED;
						}
						else
						{
							int dir2 = g->child[RIGHT] == p;
 
							if ( expire_is_red ( s->child[last] ) )
								g->child[dir2] = expire_double ( p, last );
							else if ( expire_is_red ( s->child[!last] ) )
								g->child[dir2] = expire_single ( p, last );
 
							q->color = g->child[dir2]->color = RED;
							g->child[dir2]->child[LEFT]->color = BLACK;
							g->child[dir2]->child[RIGHT]->color = BLACK;
						}
					}
				}
			}
		}
 
		if ( f != NULL )
		{
			metric_node *removed = f->metric;
			if (f != q)
			{
				f->key = q->key;
				f->metric = q->metric;
				if (f->metric)
					f->metric->expire_node = f;
			}
			/* `removed` lost its entry: either f now holds another metric, or
			   f == q and the node is freed below. Leaving the back-pointer set
			   makes the next metric_refresh_expire read a freed expire_node. */
			if (removed && (f == q || removed != f->metric))
				removed->expire_node = NULL;
			p->child[p->child[RIGHT] == q] = q->child[q->child[LEFT] == NULL];
			//p->child[last] = NULL;
			free ( q );
			excode = 1;
		}
 
		tree->root = head->child[RIGHT];
		free(head);
		if ( tree->root != NULL )
			tree->root->color = BLACK;
	}
 
	return excode;
}

/*
 * Expiry ownership. These four are the only functions allowed to mutate the
 * metric_node <-> expire_node link, so the back-pointer cannot drift and no
 * caller has to guess the stored key. The `_locked` forms require the caller to
 * already hold expiretree->rwlock; the plain forms take it.
 *
 * A timer-wheel expiry implementation only has to reimplement these four.
 */
void expire_arm_locked ( expire_tree *tree, int64_t key, metric_node *metric )
{
	if (!tree || !metric)
		return;
	if (metric->expire_node)
		expire_forget_locked(tree, metric);
	expire_insert_inner(tree, key, metric);
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
	if (!tree || !metric || !metric->expire_node)
		return;
	expire_delete_inner(tree, metric->expire_node->key, metric);
	/* expire_delete_inner clears it when it removes the entry; if the entry was
	   not in the tree the link was already stale, so drop it either way. */
	metric->expire_node = NULL;
}

void expire_forget ( expire_tree *tree, metric_node *metric )
{
	if (!tree || !metric)
		return;
	pthread_rwlock_wrlock(tree->rwlock);
	expire_forget_locked(tree, metric);
	pthread_rwlock_unlock(tree->rwlock);
}

/* Entries still expired after the purge mean a metric could not be reached
   through the metric tree. Removing them blindly used to relocate a live
   metric's entry into another node, so they are only counted and reported. */
uint64_t expire_count_expired ( expire_node *x, int64_t key )
{
	if (!x)
		return 0;
	uint64_t n = x->key <= key ? 1 : 0;
	if (x->key > key)
		return n + expire_count_expired(x->child[LEFT], key);
	return n + expire_count_expired(x->child[LEFT], key)
	         + expire_count_expired(x->child[RIGHT], key);
}

void tree_show(expire_node *x)
{
	char *color = x->color ? "red" : "black";
	glog(L_TRACE, "%"u64": '%p' (%s)\n", x->key, x->metric, color);
	if ( x->child[LEFT] )
		tree_show(x->child[LEFT]);
	if ( x->child[RIGHT] )
		tree_show(x->child[RIGHT]);
}

void expire_show ( expire_tree *tree )
{
	pthread_rwlock_rdlock(tree->rwlock);
	if ( tree && tree->root )
		tree_show(tree->root);
	pthread_rwlock_unlock(tree->rwlock);
}

void tree_free(expire_node *x)
{
	if ( x->child[LEFT] )
		tree_free(x->child[LEFT]);
	if ( x->child[RIGHT] )
		tree_free(x->child[RIGHT]);
	free (x);
}

void expire_free ( expire_tree *tree )
{
	pthread_rwlock_wrlock(tree->rwlock);
	if ( tree && tree->root )
		tree_free(tree->root);
	pthread_rwlock_unlock(tree->rwlock);
}

uint64_t tree_build(expire_node *x, uint64_t l)
{
	l++;

	char *color = x->color ? "red" : "black";
	if ( x->child[LEFT] )
		l = tree_build(x->child[LEFT], l++);

	char line[4096];
	int pos = 0;
	uint64_t i;
	if (l >= sizeof(line) / 2)
		l = sizeof(line) / 2 - 1;
	for (i = 0; i < l; i++)
		line[pos++] = '\t';
	pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "%"u64" (%p){%p} %s", x->key, x, x->metric, color);
	labels_t *lab = x->metric->labels;
	while (lab && pos < (int)sizeof(line) - 64)
	{
		pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "(%s=%s)", lab->name, lab->key);
		lab = lab->next;
	}
	glog(L_TRACE, "%s\n", line);

	if ( x->child[RIGHT] )
		l = tree_build(x->child[RIGHT], l++);
	l--;

	return l;
}

void expire_build (char *namespace)
{
	extern aconf *ac;
	expire_tree *tree = ac->nsdefault->expiretree;
	if (!namespace)
		tree = ac->nsdefault->expiretree;
	else
		return;

	if ( tree && tree->root )
	{
		pthread_rwlock_rdlock(tree->rwlock);
		tree_build(tree->root, -1);
		pthread_rwlock_unlock(tree->rwlock);
	}
}

//expire_tree* tree_get ( expire_node *x, int64_t key )
//{
//	if (  x && x->key < key )
//		return tree_get(x->child[LEFT], key);
//	else if ( x && x->key > key )
//		return tree_get(x->child[RIGHT], key);
//	else if ( x && x->key == key )
//	{
//		printf("finded: %"u64"\n", x->child[LEFT]->key);
//		return tree_get(x->child[RIGHT], key);
//	}
//	else
//		return NULL;
//}

expire_node* expire_find ( expire_tree *tree, int64_t key )
{
	pthread_rwlock_rdlock(tree->rwlock);
	if ( !tree || !(tree->root) ) {
		pthread_rwlock_unlock(tree->rwlock);
		return NULL;
	}
	expire_node *x = tree->root;
	while ( x )
	{
		if ( x->key > key )
			x = x->child[LEFT];
		else if ( x->key < key )
			x = x->child[RIGHT];
		else if ( x->key == key )
		{
			pthread_rwlock_unlock(tree->rwlock);
			return x;
		}
		else
		{
			pthread_rwlock_unlock(tree->rwlock);
			return NULL;
		}
	}
	pthread_rwlock_unlock(tree->rwlock);
	return NULL;
}

uint64_t expire_node_purge(expire_node *x, uint64_t key, metric_tree *tree, expire_tree *expiretree)
{
	uint64_t ret = 0;
	if(!x)
		return 0;
	if ( key < x->key )
	{
		if(x->child[LEFT])
			ret += expire_node_purge(x->child[LEFT], key, tree, expiretree);
	}
	else if ( key == x->key )
	{
		if (x->metric && x->metric->labels)
		{
			/* Returns to the caller's loop instead of continuing the walk:
			   metric_delete can relocate node contents, so every pointer held
			   across it would go stale. */
			ret += metric_delete_locked(tree, x->metric->labels, expiretree);
			return ret;
		}

		if(x->child[LEFT])
			ret += expire_node_purge(x->child[LEFT], key, tree, expiretree);
		if(x->child[RIGHT])
			ret += expire_node_purge(x->child[RIGHT], key, tree, expiretree);
	}
	else
	{
		if (x->metric && x->metric->labels)
		{
			ret += metric_delete_locked(tree, x->metric->labels, expiretree);
			return ret;
		}

		if(x->child[LEFT])
			ret += expire_node_purge(x->child[LEFT], key, tree, expiretree);
		if(x->child[RIGHT])
			ret += expire_node_purge(x->child[RIGHT], key, tree, expiretree);
	}
	return ret;
}

void expire_purge(uint64_t key, char *namespace, namespace_struct *ns)
{
	extern aconf *ac;

	glog(L_INFO, "run expire purge on namespace %s/%s\n", namespace, ns->key);

	if (!ns) {
		if (!namespace)
			ns = ac->nsdefault;
		else
			ns = get_namespace(namespace);
	}

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;

	/* Lock order everywhere is metric tree then expire tree. */
	pthread_rwlock_wrlock(tree->rwlock);
	pthread_rwlock_wrlock(expiretree->rwlock);

	r_time start = setrtime();
	while (expire_node_purge(expiretree->root, key, tree, expiretree));

	uint64_t orphans = expire_count_expired(expiretree->root, key);

	pthread_rwlock_unlock(expiretree->rwlock);
	pthread_rwlock_unlock(tree->rwlock);

	r_time end = setrtime();
	uint64_t expire_time = getrtime_mcs(start, end, 0);
	if (ac->nsdefault->metrictree->count)
		metric_update("alligator_gc_duration_microseconds", NULL, &expire_time, DATATYPE_UINT, NULL);

	/* Only on corruption: expire_purge also runs from namespace_free, where
	   emitting into another namespace's tree during teardown is unsafe. */
	if (orphans)
	{
		glog(L_ERROR, "expire purge on namespace '%s' left %"u64" expired entries whose metric could not be reached\n", ns->key, orphans);
		metric_add_labels("alligator_expire_orphan_entries", &orphans, DATATYPE_UINT, NULL, "namespace", ns->key);
	}
}
