#include "rbtree.h"
#include "dstructures/tommy.h"
#include "main.h"
#include "common/selector.h"

int is_red ( rb_node *node )
{
	return node != NULL && node->color == RED;
}

rb_node *rb_single ( rb_node *root, int dir )
{
	rb_node *save = root->steam[!dir];
 
	root->steam[!dir] = save->steam[dir];
	save->steam[dir] = root;
 
	root->color = RED;
	save->color = BLACK;
 
	return save;
}
 
rb_node *rb_double ( rb_node *root, int dir )
{
	root->steam[!dir] = rb_single ( root->steam[!dir], !dir );
	return rb_single ( root, dir );
}

rb_node *make_node ( rb_tree *tree,  char *key, char *name )
{
	rb_node *rn = malloc ( sizeof *rn );
  
	if ( rn != NULL ) {
		rn->key = key;
		rn->color = RED;
		rn->steam[LEFT] = NULL;
		rn->steam[RIGHT] = NULL;
		//rn->d = value;
		//gen_ngram(key, strlen(key), tree->hash, rn);
		rn->name = name;
		//rn->ts_file = ac->ts_dir;
	}
	return rn;
}


rb_node* rb_insert ( rb_tree *tree, alligator_labels *lbl )
{
		char *context = lbl->name;
		char *key = lbl->key;
		rb_node *ret = NULL;
		if ( tree->root == NULL )
		{
			tree->hash = malloc(sizeof(*(tree->hash)));
			tommy_hashdyn_init(tree->hash);
		        tree->root = ret = make_node ( tree, key, context );
			tree->name = lbl->name;
	        	if ( tree->root == NULL )
	       			return NULL;
		}
		else
		{
			rb_node head = {0};
			rb_node *g, *t;
			rb_node *p, *q;
			int dir = 0, last = 0;
		
			t = &head;
			g = p = NULL;
			q = t->steam[RIGHT] = tree->root;
		
			for (;;) 
			{
				if ( q == NULL )
				{
					p->steam[dir] = q = ret = make_node ( tree, key, lbl->name );
					tree->count ++ ;
					if ( q == NULL )
						return NULL;
				}
				else if ( is_red ( q->steam[LEFT] ) && is_red ( q->steam[RIGHT] ) ) 
				{
					q->color = RED;
					q->steam[LEFT]->color = BLACK;
					q->steam[RIGHT]->color = BLACK;
				}
				else
				{
				}
				if ( is_red ( q ) && is_red ( p ) ) 
				{
					int dir2 = t->steam[RIGHT] == g;
		
					if ( q == p->steam[last] )
				       		t->steam[dir2] = rb_single ( g, !last );
					else
				       		t->steam[dir2] = rb_double ( g, !last );
				}
				if ( !strcmp(q->key, key) )
				{
					break;
				}
				else
				{
					//printf("OK\n");
				}
		
				last = dir;
				//dir = q->key < key;
				dir = strcmp(key, q->key) > 0;
				//curcontext = q->name;
		
				if ( g != NULL )
					t = g;
				g = p, p = q;
				q = q->steam[dir];
			}
			tree->root = head.steam[RIGHT];
		}
		tree->root->color = BLACK;
		//lbl = lbl->next;
	return ret;
}

int rb_delete ( rb_tree *tree, char *key )
{
	if ( tree->root != NULL ) 
	{
		rb_node head = {0}; /* временный указатель на корень дерева */
		rb_node *q, *p, *g; /* вспомогательные переменные */
		rb_node *f = NULL;	/* узел, подлежащий удалению */
		int dir = 1;
 
		/* инициализация вспомогательных переменных */
		q = &head;
		g = p = NULL;
		q->steam[RIGHT] = tree->root;
 
		/* поиск и удаление */
		while ( q->steam[dir] != NULL )
		{
			int last = dir;
 
			/* сохранение иерархии узлов во временные переменные */
			g = p, p = q;
			q = q->steam[dir];
			//dir = q->key < key;
			dir = strcmp(key, q->key) > 0;
 
			/* сохранение найденного узла */
			if ( !strcmp(q->key, key) )
				f = q;
 
			if ( !is_red ( q ) && !is_red ( q->steam[dir] ) ) {
				if ( is_red ( q->steam[!dir] ) )
					p = p->steam[last] = rb_single ( q, dir );
				else if ( !is_red ( q->steam[!dir] ) ) {
					rb_node *s = p->steam[!last];
 

					if ( s != NULL ) {
						if ( !is_red ( s->steam[!last] ) && !is_red ( s->steam[last] ) ) {
							/* смена цвета узлов */
							p->color = BLACK;
							s->color = RED;
							q->color = RED;
						}
						else {
							int dir2 = g->steam[RIGHT] == p;
 
							if ( is_red ( s->steam[last] ) )
								g->steam[dir2] = rb_double ( p, last );
							else if ( is_red ( s->steam[!last] ) )
								g->steam[dir2] = rb_single ( p, last );
 
							/* корректировка цвета узлов */
							q->color = g->steam[dir2]->color = RED;
							g->steam[dir2]->steam[LEFT]->color = BLACK;
							g->steam[dir2]->steam[RIGHT]->color = BLACK;
						}
					}
				}
			}
		}
 
		/* удаление найденного узла */
		if ( f != NULL ) {
			free(f->key);
			f->key = q->key;
			p->steam[p->steam[RIGHT] == q] =
				q->steam[q->steam[LEFT] == NULL];
			//free ( q->info );
			free ( q );
		}
 
		/* обновление указателя на корень дерева */
		tree->root = head.steam[RIGHT];
		if ( tree->root != NULL )
			tree->root->color = BLACK;
	}
 
	return 1;
}

void tree_show(rb_node *x)
{
	printf("%s: %lf\n",x->key, x->d);
	if ( x->steam[LEFT] )
		tree_show(x->steam[LEFT]);
	if ( x->steam[RIGHT] )
		tree_show(x->steam[RIGHT]);
}

void rb_show ( rb_tree *tree )
{
	if ( tree && tree->root )
		tree_show(tree->root);
}


void rb_build ( rb_tree *tree );
uint64_t tree_build(rb_node *x, uint64_t l)
{
	l++;

	if ( x->steam[LEFT] )
		l = tree_build(x->steam[LEFT], l++);

	uint64_t i;
	for ( i=0; i<l; i++)
		printf("\t");
	printf("%s: (%lf)\n",x->key, x->d);
	if (x->stree && x->stree->root)
	{
		printf("context %s provides:\n", x->stree->name);
		//tommy_hashdyn_foreach(x->stree->hash, tommyhash_forearch);
		tree_build(x->stree->root, l);
	}

	if ( x->steam[RIGHT] )
		l = tree_build(x->steam[RIGHT], l++);
	l--;

	return l;
}

void rb_build ( rb_tree *tree )
{
	if ( tree && tree->root )
	{
		//tommy_hashdyn_foreach(tree->hash, tommyhash_forearch);
		tree_build(tree->root, 0);
	}
}

rb_tree* tree_get ( rb_node *x, char* key )
{
	//if (  x && x->key < key )
	if (  x && strcmp(key, x->key) > 0 )
		return tree_get(x->steam[LEFT], key);
	//else if ( x && x->key > key )
	else if ( x && strcmp(key, x->key) < 0 )
		return tree_get(x->steam[RIGHT], key);
	else if ( x && !strcmp(x->key, key) )
	{
		return tree_get(x->steam[RIGHT], key);
	}
	else
		return NULL;
}

rb_node* rb_find ( rb_tree *tree, char* key )
{
	if ( !tree || !(tree->root) )
		return NULL;
	rb_node *x = tree->root;
	while ( x )
	{
		//if ( x->key > key )
		if ( strcmp(x->key, key) > 0 )
			x = x->steam[LEFT];
		//else if ( x->key < key )
		else if ( strcmp(x->key, key) < 0 )
			x = x->steam[RIGHT];
		//else if ( x->key == key )
		else if ( !strcmp(key, x->key) )
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

int find_label(rb_tree *tree, alligator_labels *lbl)
{
	return 0;
}

void rb_tree_labels_for(rb_node *x, char *prefix, int slave, stlen *str)
{
	if ( !x )
		return;

	char tmp[1000];
	if ( x && x->key )
	{
		if ( slave )
			snprintf(tmp, 1000, "%s, %s=\"%s\"", prefix, x->name, x->key);
		else
			snprintf(tmp, 1000, "%s%s=\"%s\"", prefix, x->name, x->key);
		//printf("debug: key: %s:%s\n", x->name, x->key);
		//printf("debug: name: %s\n", tmp);
	}

	if ( x->steam[LEFT] )
		rb_tree_labels_for(x->steam[LEFT], prefix, slave, str);
	if (x->stree && x->stree->root)
		rb_tree_labels_for(x->stree->root, tmp, 1, str);
	else if ( x->en == ALLIGATOR_DATATYPE_DOUBLE )
	{
		//write(fd, tmp, strlen(tmp));
		//write(fd, "} ", 2);
		char charmetric[20];
		size_t charmetric_size = snprintf(charmetric, 20, "%lf", x->d);
		//write(fd, charmetric, charmetric_size);
		//write(fd, &(x->d), 8);
		//write(fd, "\n", 1);
		stlencat(str, tmp, strlen(tmp));
		stlencat(str, "} ", 2);
		stlencat(str, charmetric, charmetric_size);
		stlencat(str, "\n", 1);
	}
	else if ( x->en == ALLIGATOR_DATATYPE_INT )
	{
		//write(fd, tmp, strlen(tmp));
		//write(fd, "} ", 2);
		//write(fd, charmetric, charmetric_size);
		//write(fd, &(x->d), 8);
		//write(fd, "\n", 1);

		char charmetric[20];
		size_t charmetric_size = snprintf(charmetric, 20, "%"d64"", x->i);
		stlencat(str, tmp, strlen(tmp));
		stlencat(str, "} ", 2);
		stlencat(str, charmetric, charmetric_size);
		stlencat(str, "\n", 1);
	}
	else if ( x->en == ALLIGATOR_DATATYPE_STRING )
	{
		//write(fd, tmp, strlen(tmp));
		//write(fd, "} ", 2);
		//write(fd, x->s, strlen(x->s));
		//write(fd, "\n", 1);

		stlencat(str, tmp, strlen(tmp));
		stlencat(str, "} ", 2);
		stlencat(str, x->s, strlen(x->s));
		stlencat(str, "\n", 1);
	}

	if ( x->steam[RIGHT] )
		rb_tree_labels_for(x->steam[RIGHT], prefix, slave, str);

}

void _metric_labels_select(rb_tree *tree, char *buf, alligator_labels *lbl);
void _rb_tree_labels_for(rb_node *x, char *prefix, int slave, alligator_labels *lbl, char *name)
{
	if ( lbl )
		fprintf(stderr, "for prefix '%s', slave: %d, lbl: '%s:%s'\n", prefix, slave, lbl->name, lbl->key);
	if ( x->steam[LEFT] )
	{
		_rb_tree_labels_for(x->steam[LEFT], prefix, slave, lbl, name);
	}
	if (x->stree && x->stree->root)
	{
		if ( x->stree->name && lbl && lbl->name && !strcmp(x->stree->name,lbl->name) )
		{
			_metric_labels_select(x->stree, prefix, lbl);
		}
		else
		{
			char tmp[1000];
			if ( slave )
				snprintf(tmp, 1000, "%s, %s=\"%s\"",prefix, name, x->key);
			else
				snprintf(tmp, 1000, "%s%s=\"%s\"",prefix, name, x->key);

			_rb_tree_labels_for(x->stree->root, tmp, 1, lbl, x->stree->name);
		}
	}
	else if ( x->en )
	{
		fprintf(stderr, "%s} %lf\n", prefix, x->d);
	}

	if ( x->steam[RIGHT] )
	{
		_rb_tree_labels_for(x->steam[RIGHT], prefix, slave, lbl, name);
	}

}
void _metric_labels_select(rb_tree *tree, char *buf, alligator_labels *lbl)
{
	fprintf(stderr, "select tree->name '%s', lbl: '%s:%s'\n", tree->name, lbl->name, lbl->key);
	rb_tree *stree = tree;
	//char buf[NAMELEN];
	//buf[0]='{';
	//buf[1]=0;
	for ( ;stree && lbl && stree->name; )
	{
		rb_node *res = rb_find(stree, lbl->key);
		if ( res )
		{
			stree = res->stree;
			lbl = lbl->next;
		}
		else
			break;
		if ( buf[2]!=0 )
			strcat(buf, ",");
		strcat(buf, stree->name);
		strcat(buf, "=\"");
		strcat(buf, res->key);
		strcat(buf, "\", ");
	}
	_rb_tree_labels_for(stree->root,buf, 0, lbl, stree->name);
	//rb_tree_labels_for(stree->root,buf, 0);
	//rb_tree_labels_select_query(stree->root,"{", lbl, 0);
}
