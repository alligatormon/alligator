#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <ctype.h>
#include "expiretree.h"
#include "main.h"

#define u64	PRIu64
#define d64	PRId64
#define	RED	1
#define	BLACK	0
#define	RIGHT	1
#define	LEFT	0


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

void expire_insert ( expire_tree *tree, int64_t key, metric_node *metric )
{
	pthread_rwlock_wrlock(tree->rwlock);
	if ( tree->root == NULL )
	{
	    tree->root = expire_make_node ( key, metric );
	    if ( tree->root == NULL ) {
	        pthread_rwlock_unlock(tree->rwlock);
			return;
        }
		tree->count ++ ;
	}
	else
	{
		expire_node *head = calloc(1, sizeof(*head));
		if (!head) {
			pthread_rwlock_unlock(tree->rwlock);
			return;
		}
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
					pthread_rwlock_unlock(tree->rwlock);
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
	pthread_rwlock_unlock(tree->rwlock);
}

void expire_build (char *namespace);

int expire_delete ( expire_tree *tree, int64_t key, metric_node *metric )
{
	int lock = 0;
	if (!tree->purging) {
		lock = 1;
		pthread_rwlock_wrlock(tree->rwlock);
	}
	int excode = 0;
	if ( tree->root != NULL ) 
	{
		expire_node *head = calloc(1, sizeof(*head));
		if (!head) {
			if (lock)
				pthread_rwlock_unlock(tree->rwlock);
			return 0;
		}
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
			f->key = q->key;
			f->metric = q->metric;
			f->metric->expire_node = f;
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
 
	if (lock) {
		pthread_rwlock_unlock(tree->rwlock);
	}
	return excode;
}

void expire_select_delete ( expire_tree *tree, int64_t key )
{
	while (expire_delete(tree, key, 0)) {}
}

void tree_show(expire_node *x)
{
	char *color = x->color ? "red" : "black";
	printf("%"u64": '%p' (%s)\n",x->key, x->metric, color);
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

	uint64_t i;
	for ( i=0; i<l; i++)
		printf("\t");
	labels_t *lab = x->metric->labels;
	printf("%"u64" (%p){%p} %s", x->key, x, x->metric, color);
	while (lab)
	{
		printf("(%s=%s)", lab->name, lab->key);
		lab = lab->next;
	}

	puts("");

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
			ret += metric_delete(tree, x->metric->labels, expiretree);
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
			ret += metric_delete(tree, x->metric->labels, expiretree);
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

	if (ac->log_level > 2)
		printf("run expire purge on namespace %s/%s\n", namespace, ns->key);

	if (!ns) {
		if (!namespace)
			ns = ac->nsdefault;
		else
			ns = get_namespace(namespace);
	}

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;

	pthread_rwlock_wrlock(tree->rwlock);
	pthread_rwlock_wrlock(expiretree->rwlock);
	expiretree->purging = 1;
	tree->purging = 1;

	r_time start = setrtime();
	while (expire_node_purge(expiretree->root, key, tree, expiretree));

	expire_select_delete(expiretree, key);

	expiretree->purging = 0;
	tree->purging = 0;
	pthread_rwlock_unlock(tree->rwlock);
	pthread_rwlock_unlock(expiretree->rwlock);

	r_time end = setrtime();
	uint64_t expire_time = getrtime_mcs(start, end, 0);
	if (ac->nsdefault->metrictree->count)
		metric_update("alligator_gc_time", NULL, &expire_time, DATATYPE_UINT, NULL);
}
