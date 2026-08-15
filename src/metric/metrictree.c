#include "main.h"
#include "metric/metric_types.h"
#include "metrictree.h"
#include "expiretree.h"
#include "labels.h"
#include "query/promql.h"
#include "common/logs.h"
#include <inttypes.h>
#include <stdlib.h>
#include <math.h>
#include <pcre.h>
//#define EXPIRE_DEFAULT_SECONDS 300

int is_red ( metric_node *node )
{
	return node != NULL && node->color == RED;
}

metric_node *metric_single ( metric_node *root, int dir )
{
	metric_node *save = root->child[!dir];
 
	root->child[!dir] = save->child[dir];
	save->child[dir] = root;
 
	root->color = RED;
	save->color = BLACK;
 
	return save;
}
 
metric_node *metric_double ( metric_node *root, int dir )
{
	root->child[!dir] = metric_single ( root->child[!dir], !dir );
	return metric_single ( root, dir );
}

metric_node *make_node (metric_tree *tree, labels_t *labels, int8_t type, void *value, expire_tree *expiretree)
{
	metric_node *rn = calloc(1, sizeof *rn);

	if ( rn != NULL )
	{
		labels_cache_fill(labels, tree);
		rn->labels = labels;
		rn->color = RED;
		rn->type = type;

		if (type == DATATYPE_INT)
			rn->i = *(int64_t*)value;
		else if (type == DATATYPE_UINT)
			rn->u = *(uint64_t*)value;
		else if (type == DATATYPE_DOUBLE)
		{
			if (isnan(*(double*)value) || isinf(*(double*)value))
				rn->d = 0;
			else
				rn->d = *(double*)value;
		}
	}
	return rn;
}


metric_node* metric_insert (metric_tree *tree, labels_t *labels, int8_t type, void* value, expire_tree *expiretree, int64_t ttl)
{
	pthread_rwlock_wrlock(tree->rwlock);
	metric_node *ret = NULL;
	if ( tree->root == NULL )
	{
		tree->root = ret = make_node(tree, labels, type, value, expiretree);
		tree->count++;
		if (tree->root == NULL) {
			pthread_rwlock_unlock(tree->rwlock);
			return NULL;
		}
	}
	else
	{
		/* RB sentinel: heap avoids stack faults when caller stack is already deep (e.g. unit tests). */
		metric_node *head = calloc(1, sizeof(*head));
		if (!head) {
			pthread_rwlock_unlock(tree->rwlock);
			return NULL;
		}
		metric_node *g, *t;
		metric_node *p, *q;
		int dir = 0, last = 0;
	
		t = head;
		g = p = NULL;
		q = t->child[RIGHT] = tree->root;
		int flag = 0;
	
		for (;;) 
		{
			if ( q == NULL )
			{
				p->child[dir] = q = ret = make_node(tree, labels, type, value, expiretree);
				tree->count++;
				flag = 1;
				if ( q == NULL ) {
					free(head);
					pthread_rwlock_unlock(tree->rwlock);
					return NULL;
				}
			}
			else if ( is_red ( q->child[LEFT] ) && is_red ( q->child[RIGHT] ) ) 
			{
				q->color = RED;
				q->child[LEFT]->color = BLACK;
				q->child[RIGHT]->color = BLACK;
			}
			else
			{
			}
			if ( is_red ( q ) && is_red ( p ) ) 
			{
				int dir2 = t->child[RIGHT] == g;
	
				if ( q == p->child[last] )
					t->child[dir2] = metric_single ( g, !last );
				else
					t->child[dir2] = metric_double ( g, !last );
			}
			if (flag)
				break;
	
			last = dir;
			dir = labels_cmp(tree->sort_plan, labels, q->labels) > 0;
	
			if ( g != NULL )
				t = g;
			g = p, p = q;
			q = q->child[dir];
		}
		tree->root = head->child[RIGHT];
		free(head);
	}
	tree->root->color = BLACK;

	r_time time = setrtime();

	expire_insert(expiretree, time.sec+ttl, ret);
	pthread_rwlock_unlock(tree->rwlock);
	return ret;
}

int metric_delete (metric_tree *tree, labels_t *labels, expire_tree *expiretree)
{
	int ret = 0;
	int lock = 0;
	if (!tree->purging) {
		pthread_rwlock_wrlock(tree->rwlock);
		lock = 1;
	}
	if ( tree->root != NULL ) 
	{
		metric_node *head = calloc(1, sizeof(*head));
		if (!head) {
			if (lock)
				pthread_rwlock_unlock(tree->rwlock);
			return 0;
		}
		metric_node *q, *p, *g;
		metric_node *f = NULL;
		int dir = 1;
 
		q = head;
		g = p = NULL;
		q->child[RIGHT] = tree->root;
 
		while ( q->child[dir] != NULL )
		{
			int last = dir;
 
			g = p, p = q;
			q = q->child[dir];
			dir = labels_cmp(tree->sort_plan, labels, q->labels) > 0;

			if ( !labels_cmp(tree->sort_plan, q->labels, labels) )
				f = q;
 
			if ( !is_red ( q ) && !is_red ( q->child[dir] ) ) {
				if ( is_red ( q->child[!dir] ) )
					p = p->child[last] = metric_single ( q, dir );
				else if ( !is_red ( q->child[!dir] ) ) {
					metric_node *s = p->child[!last];
 

					if ( s != NULL ) {
						if ( !is_red ( s->child[!last] ) && !is_red ( s->child[last] ) ) {
							p->color = BLACK;
							s->color = RED;
							q->color = RED;
						}
						else {
							int dir2 = g->child[RIGHT] == p;
 
							if ( is_red ( s->child[last] ) )
								g->child[dir2] = metric_double ( p, last );
							else if ( is_red ( s->child[!last] ) )
								g->child[dir2] = metric_single ( p, last );
 
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

			if ( f == q )
			{
				/* Matched node is the node physically removed: drop its single
				   expire entry and free it. No reinsert (would dangle onto freed memory). */
				expire_delete(expiretree, q->expire_node->key, q);
				tree->count--;
				labels_free(q->labels, tree);
				p->child[p->child[RIGHT] == q] = q->child[q->child[LEFT] == NULL];
				free ( q );
				ret = 1;
			}
			else
			{
				/* q's content moves into f (RB delete: found node f, physical
				   removal of successor/leaf q). Expire tree keys are tied to the
				   metric pointer, so delete both entries and reinsert f with q's
				   key. Labels alone are not enough — value/type must move too,
				   otherwise the surviving metric keeps the deleted node's sample. */
				uint64_t q_key = q->expire_node->key;
				expire_delete(expiretree, q_key, q);
				expire_delete(expiretree, f->expire_node->key, f);
				expire_insert(expiretree, q_key, f);
				tree->count--;
				labels_free(f->labels, tree);
				if (f->percentile_buf)
					free_percentile_buffer(f->percentile_buf);
				f->labels = q->labels;
				f->type = q->type;
				f->list_len = q->list_len;
				f->enabled = q->enabled;
				f->percentile_buf = q->percentile_buf;
				f->u = q->u; /* bit-exact union copy (d/i/s/list share storage) */
				q->percentile_buf = NULL;
				//q->s = NULL;
				//q->list = NULL;
				p->child[p->child[RIGHT] == q] = q->child[q->child[LEFT] == NULL];
				free ( q );
				ret = 1;
			}
		}
 
		tree->root = head->child[RIGHT];
		free(head);
		if ( tree->root != NULL )
			tree->root->color = BLACK;
	}

	if (lock) {
		pthread_rwlock_unlock(tree->rwlock);
	}
	return ret;
}

static void metric_refresh_expire(metric_node *mnode, expire_tree *expiretree, int64_t ttl)
{
	r_time time = setrtime();
	int64_t new_key = time.sec + ttl;
	/* Expiry uses second resolution; delete+insert in the RB-tree is redundant if the key is unchanged. */
	if (mnode->expire_node && mnode->expire_node->key == new_key)
		return;
	if (mnode->expire_node)
		expire_delete(expiretree, mnode->expire_node->key, mnode);
	expire_insert(expiretree, new_key, mnode);
}

void metric_gset(metric_node *mnode, int8_t type, void* value, expire_tree *expiretree, int64_t ttl)
{
	if (type == DATATYPE_INT)
		mnode->i += *(int64_t*)value;
	else if (type == DATATYPE_UINT)
		mnode->u += *(uint64_t*)value;
	else if (type == DATATYPE_DOUBLE)
	{
		if (isnan(*(double*)value) || isinf(*(double*)value))
			{}
		else
			mnode->d += *(double*)value;
	}

	metric_refresh_expire(mnode, expiretree, ttl);
}

void metric_set(metric_node *mnode, int8_t type, void* value, expire_tree *expiretree, int64_t ttl)
{
	mnode->type = type;
	if (type == DATATYPE_INT)
		mnode->i = *(int64_t*)value;
	else if (type == DATATYPE_UINT)
		mnode->u = *(uint64_t*)value;
	else if (type == DATATYPE_DOUBLE)
	{
		if (isnan(*(double*)value) || isinf(*(double*)value))
			mnode->d = 0;
		else
			mnode->d = *(double*)value;
	}

	metric_refresh_expire(mnode, expiretree, ttl);
}

void metrictree_show(metric_node *x)
{
	labels_print(x->labels, 0);
	glog(L_TRACE, "\n");
	if ( x->child[LEFT] )
		metrictree_show(x->child[LEFT]);
	if ( x->child[RIGHT] )
		metrictree_show(x->child[RIGHT]);
}

void metric_show ( metric_tree *tree )
{
	pthread_rwlock_rdlock(tree->rwlock);
	if ( tree && tree->root )
		metrictree_show(tree->root);
	pthread_rwlock_unlock(tree->rwlock);
}


uint64_t metrictree_build(metric_node *x, uint64_t l, string *s)
{
	l++;

	if ( x->child[LEFT] )
		l = metrictree_build(x->child[LEFT], l++, s);

	//labels_print(x->labels, l);
	labels_cat(x->labels, l, s, x->expire_node->key, x->color);

	if ( x->child[RIGHT] )
		l = metrictree_build(x->child[RIGHT], l++, s);
	l--;

	return l;
}

void metric_build (char *namespace, string *s)
{
	extern aconf *ac;
	metric_tree *tree = ac->nsdefault->metrictree;
	if (!namespace)
		tree = ac->nsdefault->metrictree;
	else
		return;

	if ( tree && tree->root )
	{
		pthread_rwlock_rdlock(tree->rwlock);
		metrictree_build(tree->root, -1, s);
		pthread_rwlock_unlock(tree->rwlock);
	}
}

void metrictree_str_build(metric_node *x, string *str, namespace_struct *ns, char *last_family, size_t last_family_size, int openmetrics)
{
	if ( x->child[LEFT] )
		metrictree_str_build(x->child[LEFT], str, ns, last_family, last_family_size, openmetrics);

	labels_t *labels = x->labels;
	char family_buf[256];
	metric_family_metadata *meta = NULL;
	const char *family_name = prom_family_exposition_resolve(ns, labels->key, &meta, family_buf, sizeof(family_buf));
	size_t family_name_len = strlen(family_name);

	if (!last_family[0] || strcmp(last_family, family_name))
	{
		string_cat(str, "# HELP ", 7);
		string_cat(str, (char *)family_name, family_name_len);
		string_cat(str, " ", 1);
		if (meta && meta->help)
			prom_help_escape_and_cat(str, meta->help);
		else
			string_cat(str, (char *)family_name, family_name_len);
		string_cat(str, "\n", 1);

		string_cat(str, "# TYPE ", 7);
		string_cat(str, (char *)family_name, family_name_len);
		string_cat(str, " ", 1);
		const char *metric_type = prom_type_exposition_keyword(meta ? meta->type : METRIC_TYPE_UNTYPED, openmetrics);
		string_cat(str, (char *)metric_type, strlen(metric_type));
		string_cat(str, "\n", 1);

		strlcpy(last_family, family_name, last_family_size);
	}

	string_cat(str, labels->key, labels->key_len);

	int quotes_open = 0;
	int quotes_close = 0;
	labels = labels->next;
	while (labels)
	{
		if (!labels->key)
		{
			labels = labels->next;
			continue;
		}

		if (!quotes_open)
		{
			string_cat(str, "{", 1);
			quotes_open = 1;
		}
		else
			string_cat(str, ",", 1);
		string_cat(str, labels->name, labels->name_len);
		string_cat(str, "=\"", 2);
		string_cat(str, labels->key, labels->key_len);
		string_cat(str, "\"", 1);

		labels = labels->next;
	}

	if (quotes_open && !quotes_close)
		string_cat(str, "}", 1);

	string_cat(str, " ", 1);

	int8_t type = x->type;
	if (type == DATATYPE_INT)
		string_int(str, x->i);
	else if (type == DATATYPE_UINT)
		string_uint(str, x->u);
	else if (type == DATATYPE_DOUBLE)
		string_double(str, x->d);
	string_cat(str, "\n", 1);

	if ( x->child[RIGHT] )
		metrictree_str_build(x->child[RIGHT], str, ns, last_family, last_family_size, openmetrics);
}

void metric_str_build (char *namespace, string *str, int openmetrics)
{
	extern aconf *ac;

	namespace_struct *ns = get_namespace_or_null(namespace);
	if (!ns)
		return;
	metric_tree *tree = ns->metrictree;

	if (tree && tree->root)
	{
		char last_family[256] = "";
		pthread_rwlock_rdlock(tree->rwlock);
		metrictree_str_build(tree->root, str, ns, last_family, sizeof(last_family), openmetrics);
		string_cat(str, "alligator_metrics_exposed_total ", 32);
		string_int(str, (tree->count + 1));
		string_cat(str, "\n", 1);
		pthread_rwlock_unlock(tree->rwlock);
	}
}

/* Name is the primary BST key. After the first exact-name hit, descendants may
 * still include other metric names. Re-check the name on every node; when a
 * name filter is set, prune branches that cannot contain the target name.
 * __name__=~ / !~ cannot use the BST: walk the whole tree and test each name. */
static int metrictree_query_name_ok(labels_t *node_labels, labels_t *query_labels, metric_query_context *mqc)
{
	if (mqc && mqc->name_re_invalid)
		return 0;
	if (mqc && mqc->name_re)
	{
		pcre *re = mqc->name_re;
		const char *n = (node_labels && node_labels->key) ? node_labels->key : "";
		int ovector[30];
		int rc = pcre_exec(re, NULL, n, (int)strlen(n), 0, 0, ovector, 30);
		int matched = rc >= 0;
		return mqc->name_re_neg ? !matched : matched;
	}
	if (!query_labels || !query_labels->key_len)
		return 1;
	return metric_name_match(node_labels, query_labels) == 0;
}

static int metrictree_query_uses_regex(metric_query_context *mqc)
{
	return mqc && (mqc->name_re || mqc->name_re_invalid);
}

void metrictree_gen_scan(metric_node *x, sortplan* sort_plan, labels_t* labels, string *groupkey, alligator_ht *hash, size_t labels_count, double opval, metric_query_context *mqc)
{
	if (!x)
		return;

	if (metrictree_query_uses_regex(mqc))
	{
		if (metrictree_query_name_ok(x->labels, labels, mqc) && !labels_match(sort_plan, x->labels, labels, labels_count))
			labels_gen_metric(x->labels, 0, x, groupkey, hash, opval);
		metrictree_gen_scan(x->child[LEFT], sort_plan, labels, groupkey, hash, labels_count, opval, mqc);
		metrictree_gen_scan(x->child[RIGHT], sort_plan, labels, groupkey, hash, labels_count, opval, mqc);
		return;
	}

	int rc = (labels && labels->key_len) ? metric_name_match(x->labels, labels) : 0;

	if ((!labels || !labels->key_len || !rc) && !labels_match(sort_plan, x->labels, labels, labels_count))
		labels_gen_metric(x->labels, 0, x, groupkey, hash, opval);

	if (labels && labels->key_len)
	{
		if (rc > 0)
			metrictree_gen_scan(x->child[LEFT], sort_plan, labels, groupkey, hash, labels_count, opval, mqc);
		else if (rc < 0)
			metrictree_gen_scan(x->child[RIGHT], sort_plan, labels, groupkey, hash, labels_count, opval, mqc);
		else
		{
			metrictree_gen_scan(x->child[LEFT], sort_plan, labels, groupkey, hash, labels_count, opval, mqc);
			metrictree_gen_scan(x->child[RIGHT], sort_plan, labels, groupkey, hash, labels_count, opval, mqc);
		}
		return;
	}

	metrictree_gen_scan(x->child[LEFT], sort_plan, labels, groupkey, hash, labels_count, opval, mqc);
	metrictree_gen_scan(x->child[RIGHT], sort_plan, labels, groupkey, hash, labels_count, opval, mqc);
}

void metrictree_gen(metric_tree *tree, labels_t* labels, string *groupkey, alligator_ht *hash, size_t labels_count, double opval, metric_query_context *mqc)
{
	pthread_rwlock_rdlock(tree->rwlock);
	if (metrictree_query_uses_regex(mqc))
	{
		if (!mqc->name_re_invalid)
			metrictree_gen_scan(tree->root, tree->sort_plan, labels, groupkey, hash, labels_count, opval, mqc);
		pthread_rwlock_unlock(tree->rwlock);
		return;
	}

	metric_node *x = tree->root;
	while (x)
	{
		int rc1 = metric_name_match(x->labels, labels);
		if ( rc1 > 0 )
			x = x->child[LEFT];
		else if ( rc1 < 0 )
			x = x->child[RIGHT];
		else
		{
			if (!labels_match(tree->sort_plan, x->labels, labels, labels_count))
				labels_gen_metric(x->labels, 0, x, groupkey, hash, opval);
			metrictree_gen_scan(x->child[LEFT], tree->sort_plan, labels, groupkey, hash, labels_count, opval, mqc);
			metrictree_gen_scan(x->child[RIGHT], tree->sort_plan, labels, groupkey, hash, labels_count, opval, mqc);
			break;
		}
	}
	pthread_rwlock_unlock(tree->rwlock);
}

void metrictree_serialize_query_node(metric_node *x, sortplan *sort_plan, labels_t* labels, string *groupkey, serializer_context *sc, size_t labels_count, metric_query_context *mqc)
{
	if (!x)
		return;

	size_t cap = 32;
	size_t sp = 0;
	metric_node **stack = malloc(cap * sizeof(*stack));
	if (!stack)
		return;

	stack[sp++] = x;

	while (sp > 0) {
		metric_node *n = stack[--sp];
		int regex = metrictree_query_uses_regex(mqc);
		int rc = (!regex && labels && labels->key_len) ? metric_name_match(n->labels, labels) : 0;

		if (metrictree_query_name_ok(n->labels, labels, mqc) && !labels_match(sort_plan, n->labels, labels, labels_count))
			metric_node_serialize(n, sc);

		int visit_left = 1;
		int visit_right = 1;
		if (!regex && labels && labels->key_len)
		{
			if (rc > 0)
				visit_right = 0;
			else if (rc < 0)
				visit_left = 0;
		}

		if (visit_right && n->child[RIGHT]) {
			if (sp >= cap) {
				size_t ncap = cap * 2;
				metric_node **ns = realloc(stack, ncap * sizeof(*stack));
				if (!ns) {
					free(stack);
					return;
				}
				stack = ns;
				cap = ncap;
			}
			stack[sp++] = n->child[RIGHT];
		}
		if (visit_left && n->child[LEFT]) {
			if (sp >= cap) {
				size_t ncap = cap * 2;
				metric_node **ns = realloc(stack, ncap * sizeof(*stack));
				if (!ns) {
					free(stack);
					return;
				}
				stack = ns;
				cap = ncap;
			}
			stack[sp++] = n->child[LEFT];
		}
	}

	free(stack);
}

void metrictree_serialize_query(metric_tree *tree, labels_t* labels, string *groupkey, serializer_context *sc, size_t labels_count, metric_query_context *mqc)
{
	pthread_rwlock_rdlock(tree->rwlock);
	if (metrictree_query_uses_regex(mqc))
	{
		if (!mqc->name_re_invalid)
			metrictree_serialize_query_node(tree->root, tree->sort_plan, labels, groupkey, sc, labels_count, mqc);
		pthread_rwlock_unlock(tree->rwlock);
		return;
	}

	metric_node *x = tree->root;
	while (x)
	{
		int rc1 = metric_name_match(x->labels, labels);
		if ( rc1 > 0 )
			x = x->child[LEFT];
		else if ( rc1 < 0 )
			x = x->child[RIGHT];
		else
		{
			if (!labels_match(tree->sort_plan, x->labels, labels, labels_count))
				metric_node_serialize(x, sc);
			metrictree_serialize_query_node(x->child[LEFT], tree->sort_plan, labels, groupkey, sc, labels_count, mqc);
			metrictree_serialize_query_node(x->child[RIGHT], tree->sort_plan, labels, groupkey, sc, labels_count, mqc);
			break;
		}
	}
	pthread_rwlock_unlock(tree->rwlock);
}

metric_node* metric_find ( metric_tree *tree, labels_t* labels )
{
	if ( !tree || !(tree->root) )
		return NULL;

	pthread_rwlock_rdlock(tree->rwlock);
	metric_node *x = tree->root;
	while ( x )
	{
		int rc1 = labels_cmp(tree->sort_plan, x->labels, labels);
		if ( rc1 > 0 )
			x = x->child[LEFT];
		else if ( rc1 < 0 )
			x = x->child[RIGHT];
		else if ( !rc1 )
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

void metrictree_free(metric_node *x)
{
	if ( x->child[LEFT] )
		metrictree_free(x->child[LEFT]);
	if ( x->child[RIGHT] )
		metrictree_free(x->child[RIGHT]);
	if ( x->percentile_buf ) {
        free_percentile_buffer(x->percentile_buf);
    }
	free (x);
}

void metric_free ( metric_tree *tree )
{
	pthread_rwlock_wrlock(tree->rwlock);
	if ( tree && tree->root )
		metrictree_free(tree->root);
	pthread_rwlock_unlock(tree->rwlock);
}

int64_t metric_get_int(void *value, int8_t type)
{
	int64_t ret;
	if (type == DATATYPE_INT)
		ret = *(int64_t*)value;
	else if (type == DATATYPE_UINT)
		ret = *(uint64_t*)value;
	else if (type == DATATYPE_DOUBLE)
	{
		if (isnan(*(double*)value) || isinf(*(double*)value))
			ret = 0;
		else
			ret = *(double*)value;
	}
	else
		ret = 0;

	return ret;
}

int64_t metric_get_double(void *value, int8_t type)
{
	double ret;
	if (type == DATATYPE_INT)
		ret = *(int64_t*)value;
	else if (type == DATATYPE_UINT)
		ret = *(uint64_t*)value;
	else if (type == DATATYPE_DOUBLE)
	{
		if (isnan(*(double*)value) || isinf(*(double*)value))
			ret = 0;
		else
			ret = *(double*)value;
	}
	else
		ret = 0;

	return ret;
}

// examples for use
///	sort_plan = malloc(sizeof(*sort_plan));
///	sort_plan->plan[0] = "__name__";
///	sort_plan->size = 1;
///	tree = calloc(1, sizeof(*tree));
///	expiretree = calloc(1, sizeof(*tree));
///	
///	labels_words_hash = alligator_ht_init(NULL);
///
///	expire_purge(expiretree, 100000000000000);
///	//metric_build(tree);
///	//metric_free(tree);
///	//expire_insert(expiretree, 1, 0);
///	//expire_build(expiretree);
///	//metric_build(tree);
///
///	string *str = string_init(10000000);
///	metric_str_build(tree, str);
///	printf("%s\n", str->s);
///
///	expire_purge(expiretree, 100000000);
///	expire_free(expiretree);
