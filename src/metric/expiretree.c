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
	expire_node *save = root->steam[!dir];
 
	root->steam[!dir] = save->steam[dir];
	save->steam[dir] = root;
 
	root->color = RED;
	save->color = BLACK;
 
	return save;
}
 
expire_node *expire_double ( expire_node *root, int dir )
{
	root->steam[!dir] = expire_single ( root->steam[!dir], !dir );
	return expire_single ( root, dir );
}

expire_node *expire_make_node ( int64_t key, metric_node *metric )
{
	expire_node *rn = malloc ( sizeof *rn );
  
	if ( rn != NULL ) {
		rn->key = key;
		rn->color = RED;
		rn->steam[LEFT] = NULL;
		rn->steam[RIGHT] = NULL;
		rn->metric = metric;
		metric->expire_node = rn;
	}
	return rn;
}

void expire_insert ( expire_tree *tree, int64_t key, metric_node *metric )
{
	if ( tree->root == NULL )
	{
		tree->count ++ ;
	        tree->root = expire_make_node ( key, metric );
	        if ( tree->root == NULL )
	       		return;
	}
	else
	{
		expire_node head = {0};
		expire_node *g, *t;
		expire_node *p, *q;
		int dir = 0, last = 0;

		t = &head;
		g = p = NULL;
		q = t->steam[RIGHT] = tree->root;

		for (;;) 
		{
			int flag = 0;
			if ( q == NULL )
			{
				flag = 1;
				p->steam[dir] = q = expire_make_node ( key, metric );
				tree->count ++ ;
				if ( q == NULL )
					return;
			}
			else if ( expire_is_red ( q->steam[LEFT] ) && expire_is_red ( q->steam[RIGHT] ) ) 
			{
				q->color = RED;
				q->steam[LEFT]->color = BLACK;
				q->steam[RIGHT]->color = BLACK;
			}

			if ( expire_is_red ( q ) && expire_is_red ( p ) ) 
			{
				int dir2 = t->steam[RIGHT] == g;
				if ( q == p->steam[last] )
					t->steam[dir2] = expire_single ( g, !last );
				else
					t->steam[dir2] = expire_double ( g, !last );
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
			q = q->steam[dir];
		}
		tree->root = head.steam[RIGHT];
	}
	tree->root->color = BLACK;
}

void expire_build (char *namespace);

int expire_delete ( expire_tree *tree, int64_t key, metric_node *metric )
{
	int excode = 0;
	if ( tree->root != NULL ) 
	{
		expire_node head = {0};
		expire_node *q, *p, *g;
		expire_node *f = NULL;
		int dir = 1;
 
		q = &head;
		g = p = NULL;
		q->steam[RIGHT] = tree->root;
		int last = dir;
 
		while ( q->steam[dir] != NULL )
		{
			last = dir;
 
			g = p, p = q;
			q = q->steam[dir];

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
 
			if ( !expire_is_red ( q ) && !expire_is_red ( q->steam[dir] ) )
			{
				if ( expire_is_red ( q->steam[!dir] ) )
					p = p->steam[last] = expire_single ( q, dir );
				else if ( !expire_is_red ( q->steam[!dir] ) )
				{
					expire_node *s = p->steam[!last];

					if ( s != NULL )
					{
						if ( !expire_is_red ( s->steam[!last] ) && !expire_is_red ( s->steam[last] ) )
						{
							p->color = BLACK;
							s->color = RED;
							q->color = RED;
						}
						else
						{
							int dir2 = g->steam[RIGHT] == p;
 
							if ( expire_is_red ( s->steam[last] ) )
								g->steam[dir2] = expire_double ( p, last );
							else if ( expire_is_red ( s->steam[!last] ) )
								g->steam[dir2] = expire_single ( p, last );
 
							q->color = g->steam[dir2]->color = RED;
							g->steam[dir2]->steam[LEFT]->color = BLACK;
							g->steam[dir2]->steam[RIGHT]->color = BLACK;
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
			p->steam[p->steam[RIGHT] == q] = q->steam[q->steam[LEFT] == NULL];
			//p->steam[last] = NULL;
			free ( q );
			excode = 1;
		}
 
		tree->root = head.steam[RIGHT];
		if ( tree->root != NULL )
			tree->root->color = BLACK;
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
	if ( x->steam[LEFT] )
		tree_show(x->steam[LEFT]);
	if ( x->steam[RIGHT] )
		tree_show(x->steam[RIGHT]);
}

void expire_show ( expire_tree *tree )
{
	if ( tree && tree->root )
		tree_show(tree->root);
}

void tree_free(expire_node *x)
{
	if ( x->steam[LEFT] )
		tree_free(x->steam[LEFT]);
	if ( x->steam[RIGHT] )
		tree_free(x->steam[RIGHT]);
	free (x);
}

void expire_free ( expire_tree *tree )
{
	if ( tree && tree->root )
		tree_free(tree->root);
}

uint64_t tree_build(expire_node *x, uint64_t l)
{
	l++;

	char *color = x->color ? "red" : "black";
	if ( x->steam[LEFT] )
		l = tree_build(x->steam[LEFT], l++);

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

	if ( x->steam[RIGHT] )
		l = tree_build(x->steam[RIGHT], l++);
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
		tree_build(tree->root, -1);
}

//expire_tree* tree_get ( expire_node *x, int64_t key )
//{
//	if (  x && x->key < key )
//		return tree_get(x->steam[LEFT], key);
//	else if ( x && x->key > key )
//		return tree_get(x->steam[RIGHT], key);
//	else if ( x && x->key == key )
//	{
//		printf("finded: %"u64"\n", x->steam[LEFT]->key);
//		return tree_get(x->steam[RIGHT], key);
//	}
//	else
//		return NULL;
//}

expire_node* expire_find ( expire_tree *tree, int64_t key )
{
	if ( !tree || !(tree->root) )
		return NULL;
	expire_node *x = tree->root;
	while ( x )
	{
		if ( x->key > key )
			x = x->steam[LEFT];
		else if ( x->key < key )
			x = x->steam[RIGHT];
		else if ( x->key == key )
		{
			return x;
		}
		else
		{
			return NULL;
		}
	}
	return NULL;
}

uint64_t expire_node_purge(expire_node *x, uint64_t key, metric_tree *tree, expire_tree *expiretree)
{
	uint64_t ret = 0;
	if(!x)
		return 0;
	if ( key < x->key )
	{
		if(x->steam[LEFT])
			ret += expire_node_purge(x->steam[LEFT], key, tree, expiretree);
	}
	else if ( key == x->key )
	{
		if (x->metric && x->metric->labels)
		{
			metric_delete(tree, x->metric->labels, expiretree);
			return ++ret;
		}

		if(x->steam[LEFT])
			ret += expire_node_purge(x->steam[LEFT], key, tree, expiretree);
		if(x->steam[RIGHT])
			ret += expire_node_purge(x->steam[RIGHT], key, tree, expiretree);
	}
	else
	{
		if (x->metric && x->metric->labels)
		{
			metric_delete(tree, x->metric->labels, expiretree);
			return ++ret;
		}

		if(x->steam[LEFT])
			ret += expire_node_purge(x->steam[LEFT], key, tree, expiretree);
		if(x->steam[RIGHT])
			ret += expire_node_purge(x->steam[RIGHT], key, tree, expiretree);
	}
	return ret;
}

void expire_purge(uint64_t key, char *namespace, namespace_struct *ns)
{
	extern aconf *ac;

	if (ac->log_level > 2)
		printf("run expire purge\n");

	if (!ns) {
		if (!namespace)
			ns = ac->nsdefault;
		else
			ns = get_namespace(namespace);
	}

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;

	r_time start = setrtime();
	while (expire_node_purge(expiretree->root, key, tree, expiretree));

	expire_select_delete(expiretree, key);

	r_time end = setrtime();
	uint64_t expire_time = getrtime_mcs(start, end, 0);
	metric_update("alligator_gc_time", NULL, &expire_time, DATATYPE_UINT, NULL);
}
