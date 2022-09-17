#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "metric/labels.h"
#include "metric/expiretree.h"
#include "dstructures/tommy.h"
#include "metric/namespace.h"
#include "common/mapping.h"
#include "query/promql.h"
#include "action/action.h"
#include "main.h"
extern aconf *ac;

int64_t get_ttl(context_arg *carg)
{
	if (!carg)
		return ac->ttl;

	//printf("curr ttl: %d, carg->ttl %d, ttl %d\n", carg->curr_ttl, carg->ttl, ac->ttl);
	if (carg->curr_ttl > -1)
	{
		if (carg->curr_ttl == 0)
		{
			if (carg->ttl == 0)
				return INT64_MAX-UINT32_MAX; // LLONG_MAX for full clean tree
			else
				return carg->ttl;
		}

		return carg->curr_ttl;
	}

	if (carg->ttl > -1)
	{
		if (carg->ttl == 0)
		{
			if (ac->ttl == 0)
				return INT64_MAX-UINT32_MAX; // LLONG_MAX for full clean tree
			else
				return ac->ttl;
		}

		return carg->ttl;
	}

	if (ac->ttl == 0)
		return INT64_MAX-UINT32_MAX;

	return ac->ttl;
}

uint8_t numbercheck(char *str)
{
	for (uint64_t j = 0; str[j]; j++)
		if(str[j] >= '0' && str[j] <= '9')
			{}
		else
			return 0;

	return 1;
}

int labels_cmp(labels_t *labels1, labels_t *labels2)
{
	sortplan *sort_plan = ac->nsdefault->metrictree->sort_plan;

	int64_t i;
	size_t plan_size = sort_plan->size;
	for (i=0; i<plan_size && labels1 && labels2; i++)
	{
		//printf("1check %s <> %s\n", sort_plan->plan[i], labels1->name);
		if (strcmp(sort_plan->plan[i], labels1->name))
			return -1;
		//printf("2check %s <> %s\n", sort_plan->plan[i], labels2->name);
		if (strcmp(sort_plan->plan[i], labels2->name))
			return 1;

		char *str1 = labels1->key;
		char *str2 = labels2->key;

		if (!str1 && !str2)
		{
			// equal
			labels1 = labels1->next;
			labels2 = labels2->next;
			continue;
		}
		if (!str1)
			return -1;
		if (!str2)
			return 1;
		//printf("labels1->key '%s'\n", str1);
		//printf("labels2->key '%s'\n", str2);

		size_t str1_len = labels1->key_len;
		size_t str2_len = labels2->key_len;

		size_t size = str1_len > str2_len ? str2_len : str1_len;
		int ret = strncmp(str1, str2, size);
		if (!ret)
			if (str1_len == str2_len)
			{
				// equal
				labels1 = labels1->next;
				labels2 = labels2->next;
				continue;
			}
			else if (str1_len > str2_len)
				return 1;
			else
				return -1;
		else
			return ret;
	}
	return 0;
}

int labels_match(labels_t *labels1, labels_t *labels2, size_t labels_count)
{
	sortplan *sort_plan = ac->nsdefault->metrictree->sort_plan;
	if (labels_count)
		++labels_count;

	int64_t i;
	size_t plan_size = sort_plan->size;
	for (i=0; i<plan_size && labels_count; i++)
	{
		//printf("plan: %d<%d: %s: %zu\n", i, plan_size, sort_plan->plan[i], labels_count);

		if (!labels1)
		{
			return -1;
		}

		if (!labels2)
		{
			return 0;
		}

		//printf("\t1check %s <> %s\n", sort_plan->plan[i], labels1->name);
		if (strcmp(sort_plan->plan[i], labels1->name))
		{
			return -1;
		}
		//printf("\t2check %s <> %s\n", sort_plan->plan[i], labels2->name);
		if (strcmp(sort_plan->plan[i], labels2->name))
		{
			return 1;
		}

		char *str1 = labels1->key;
		char *str2 = labels2->key;

		if (!str1 && !str2)
		{
			// equal
			labels1 = labels1->next;
			labels2 = labels2->next;
			continue;
		}
		if (!str1)
		{
			// TODO??? maybe next???
			continue;
		}
		if (!str2)
		{
			labels1 = labels1->next;
			labels2 = labels2->next;
			continue;
		}
		//printf("\t\tlabels1->key '%s'\n", str1);
		//printf("\t\tlabels2->key '%s'\n", str2);

		size_t str1_len = labels1->key_len;
		size_t str2_len = labels2->key_len;

		size_t size = str1_len > str2_len ? str2_len : str1_len;
		int ret = strncmp(str1, str2, size);
		if (!ret)
			if (str1_len == str2_len)
			{
				// equal
				--labels_count;
				labels1 = labels1->next;
				labels2 = labels2->next;
				continue;
			}
			else if (str1_len > str2_len)
			{
				return 1;
			}
			else
			{
				return -1;
			}
		else
		{
			return ret;
		}
	}
	//printf("return result labels count %zu\n", labels_count);
	return labels_count;
}

int metric_name_match(labels_t *labels1, labels_t *labels2)
{
	if (!labels1 || !labels2)
		return 0;

	if (!labels2->key_len)
		return 0;

	char *str1 = labels1->key;
	char *str2 = labels2->key;
	return strcmp(str1, str2);
}

void labels_print(labels_t *labels, int l)
{
	labels_t *cur = labels;

	while (cur)
	{
		uint64_t i;
		for ( i=0; i<l; i++)
			printf("\t");
		printf("%s:%s (%zu/%zu), go to: %p\n", cur->name, cur->key, cur->name_len, cur->key_len, cur->next);
		cur = cur->next;
	}
}

void labels_cat(labels_t *labels, int l, string *s, int64_t ttl, int color)
{
	labels_t *cur = labels;
	char tmps[1000];

	while (cur)
	{
		uint64_t i;
		for ( i=0; i<l; i++)
			string_cat(s, "\t", 1);
		int64_t size = snprintf(tmps, 1000, "%s:%s (%zu/%zu), ttl: %"d64", color: %s, go to: %p\n", cur->name, cur->key, cur->name_len, cur->key_len, ttl, color ? "RED" : "BLACK", cur->next);
		string_cat(s, tmps, size);
		cur = cur->next;
	}
}

int query_struct_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((query_struct*)obj)->key;
        return strcmp(s1, s2);
}

string *labels_to_groupkey(labels_t *labels_list, string *groupkey)
{
	if (!groupkey || !groupkey->l)
	{
		return string_init_alloc("", 0);
	}

	string *label = string_init(255);
	uint64_t j = 0;
	char tmp[255];
	//printf("groupkey '%s'\n",  groupkey->s);
	for (uint64_t i = 0; i < groupkey->l; i++)
	{
		j = strcspn(groupkey->s + i, ",");
		strlcpy(tmp, groupkey->s + i, j+1);

		labels_t *cur_labels = labels_list;
		while (cur_labels)
		{
			if (!strcmp(cur_labels->name, tmp))
			{
				string_cat(label, cur_labels->key, cur_labels->key_len);
				string_cat(label, ", ", 2);
			}
			cur_labels = cur_labels->next;
		}
	}

	return label;
}

alligator_ht *labels_to_hash(labels_t *labels_list, string *groupkey)
{
	alligator_ht *lbl = NULL;
	if (!groupkey || !groupkey->l)
	{
		return lbl;
	}

	uint64_t j = 0;
	char tmp[255];
	//printf("groupkey '%s'\n",  groupkey->s);
	for (uint64_t i = 0; i < groupkey->l; i++)
	{
		j = strcspn(groupkey->s + i, ",");
		strlcpy(tmp, groupkey->s + i, j+1);

		labels_t *cur_labels = labels_list;
		while (cur_labels)
		{
			if (!strcmp(cur_labels->name, tmp))
			{
				if (!lbl)
				{
					lbl = alligator_ht_init(NULL);
				}

				labels_hash_insert_nocache(lbl, cur_labels->name, cur_labels->key);
			}
			cur_labels = cur_labels->next;
		}
	}

	return lbl;
}

void labels_gen_metric(labels_t *labels_list, int l, metric_node *x, string *groupkey, alligator_ht *res_hash)
{
	if (!labels_list)
		return;

	string *key = labels_to_groupkey(labels_list, groupkey);
	uint32_t key_hash = tommy_strhash_u32(0, key->s);
	query_struct *qs = alligator_ht_search(res_hash, query_struct_compare, key->s, key_hash);
	//printf("labels->list '%s': %p: %lf\n", labels_list->key, qs, x->d);
	if (!qs)
	{
		qs = malloc(sizeof(*qs));
		qs->val = 0;
		qs->lbl = labels_to_hash(labels_list, groupkey);
		qs->count = 1;
		qs->key = key->s;
		qs->metric_name = labels_list->key;
		alligator_ht_insert(res_hash, &(qs->node), qs, key_hash);

		int8_t type = x->type;
		if (type == DATATYPE_INT)
			qs->val += x->i;
		else if (type == DATATYPE_UINT)
			qs->val += x->u;
		else if (type == DATATYPE_DOUBLE)
			qs->val += x->d;

		qs->min = qs->val;
		qs->max = qs->val;
	}
	else
	{
		free(key->s);

		++qs->count;
		int8_t type = x->type;
		if (type == DATATYPE_INT)
		{
			qs->val += x->i;
			if (qs->min > x->i)
				qs->min = x->i;
			if (qs->max < x->i)
				qs->max = x->i;
		}
		else if (type == DATATYPE_UINT)
		{
			qs->val += x->u;
			if (qs->min > x->u)
				qs->min = x->u;
			if (qs->max < x->u)
				qs->max = x->u;
		}
		else if (type == DATATYPE_DOUBLE)
		{
			qs->val += x->d;
			if (qs->min > x->d)
				qs->min = x->d;
			if (qs->max < x->d)
				qs->max = x->d;
		}
	}

	free(key);
}

int labels_hash_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((labels_container*)obj)->name;
        return strcmp(s1, s2);
}

int labels_word_hash_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((labels_words_cache*)obj)->w;
        return strcmp(s1, s2);
}

void labels_free(labels_t *labels, metric_tree *metrictree)
{
	if (!labels)
		return;


	labels_words_cache *labels_cache;
	alligator_ht *labels_words_hash = metrictree->labels_words_hash;
	
	while (labels)
	{
		uint32_t name_hash = tommy_strhash_u32(0, labels->name);
		labels_cache = alligator_ht_search(labels_words_hash, labels_word_hash_compare, labels->name, name_hash);
		if (labels_cache)
		{
			labels_cache->count--;
			if (labels_cache->count == 0)
			{
				free(labels_cache->w);
				alligator_ht_remove_existing(labels_words_hash, &(labels_cache->node));
				++ac->metric_freed;
			}
		}
		else
		{
			fprintf(stderr, "unknown error 10\n");
		}

		if (!labels->key)
		{
			// unfreeed exit
			//return;
			labels_t *labels_old = labels;
			labels = labels->next;
			// freed exit
			free(labels_old);

			continue;
		}
		uint32_t key_hash = tommy_strhash_u32(0, labels->key);
		labels_cache = alligator_ht_search(labels_words_hash, labels_word_hash_compare, labels->key, key_hash);
		if (labels_cache)
		{
			labels_cache->count--;
			if (labels_cache->count == 0)
			{
				free(labels_cache->w);
				alligator_ht_remove_existing(labels_words_hash, &(labels_cache->node));
				++ac->metric_freed;
			}
		}
		else
		{
			fprintf(stderr, "unknown error 11\n");
		}

		labels_t *labels_old = labels;
		labels = labels->next;
		// freed exit
		free(labels_old);
	}
}

void labels_new_plan_node(void *funcarg, void* arg)
{
	labels_container *labelscont = arg;
	if (!labelscont)
		return;

	// add element to labelscont
	labels_t *cur = funcarg;
	sortplan *sort_plan = cur->sort_plan;
	while(cur->next)
		cur = cur->next;
	cur->next = malloc(sizeof(labels_t));
	cur = cur->next;
	cur->name = strdup(labelscont->name);
	cur->name_len = strlen(cur->name);
	cur->key = strdup(labelscont->key);
	cur->key_len = strlen(cur->key);
	cur->next = 0;

	cur->allocatedname = 1;
	cur->allocatedkey = 1;

	// add element to sortplan
	sort_plan->plan[(sort_plan->size)++] = cur->name;

	free(labelscont);
}


void labels_head_free(labels_t *labels)
{
	labels_t *new;
	while (labels)
	{
		new = labels->next;
		if(labels->allocatedkey)
		{
			//printf("DEBUG1: free key %p (%s)\n", labels->key, labels->key);
			free(labels->key);
		}
		free(labels);
		labels = new;
	}
}

void labels_merge_for(void *funcarg, void* arg)
{
	alligator_ht *dst = funcarg;
	labels_container *labelscont = arg;

	labels_hash_insert_nocache(dst, labelscont->name, labelscont->key);
}

void labels_merge(alligator_ht *dst, alligator_ht *src)
{
	alligator_ht_foreach_arg(src, labels_merge_for, dst);
}

labels_t* labels_initiate(alligator_ht *hash, char *name, char *namespace, namespace_struct *arg_ns, uint8_t no_del)
{
	namespace_struct *ns;

	if ((!namespace) && !(arg_ns))
		ns = ac->nsdefault;
	else if(arg_ns)
		ns = arg_ns;
	else // add support namespaces
		return NULL;

	if (!hash)
	{
		hash = alligator_ht_init(NULL);
	}

	labels_t *labels = malloc(sizeof(*labels));
	labels->name = "__name__";
	labels->name_len = 8;
	labels->key = name;
	if (name)
		labels->key_len = strlen(name);
	else
		labels->key_len = 0;

	labels->allocatedname = 0;
	labels->allocatedkey = 0;

	labels->sort_plan = ns->metrictree->sort_plan;
	sortplan *sort_plan = ns->metrictree->sort_plan;

	labels_t *cur = labels;

	uint64_t i;
	for (i=1; i<sort_plan->size; i++)
	{
		cur->next = calloc(1, sizeof(labels_t));
		cur = cur->next;
		cur->sort_plan = sort_plan;
		cur->name = sort_plan->plan[i];
		cur->name_len = strlen(cur->name);
		//printf ("ns %p, nskey %s, hash %p, sort_plan %p, sort_plan->size %d, i %d plan '%s'\n", ns, ns->key, hash, sort_plan, sort_plan->size, i, sort_plan->plan[i]);
		//printf ("\tsort_plan->plan '%s' %p\n", hash, &sort_plan->plan[i], sort_plan->plan[i]);
		labels_container *labelscont = alligator_ht_search(hash, labels_hash_compare, sort_plan->plan[i], tommy_strhash_u32(0, sort_plan->plan[i]));
		if (labelscont)
		{
			cur->key = labelscont->key;
			cur->key_len = strlen(cur->key);
			cur->allocatedname = labelscont->allocatedname;
			cur->allocatedkey = labelscont->allocatedkey;
			if (labelscont->allocatedname)
			{
				labelscont->allocatedname = 0;
				free(labelscont->name);
			}
			if (!no_del)
				alligator_ht_remove_existing(hash, &(labelscont->node));

			free(labelscont);
		}
		else
		{
			cur->allocatedname = 0;
			cur->allocatedkey = 0;
			cur->key = 0;
			cur->key_len = 0;
		}

	}

	cur->next = 0;
	if (!no_del)
	{
		alligator_ht_foreach_arg(hash, labels_new_plan_node, cur);
		alligator_ht_done(hash);
		free(hash);
	}

	return labels;
}

void labels_free_node(void *funcarg, void* arg)
{
	labels_container *labelscont = arg;
	if (!labelscont)
		return;

	if (labelscont->allocatedkey)
	{
		//printf("DEBUG1: free key %p (%s)\n", labelscont->key, labelscont->key);
		free(labelscont->key);
	}
	if (labelscont->allocatedname)
		free(labelscont->name);

	free(labelscont);
}

void labels_hash_free(alligator_ht *hash)
{
	if (!hash)
		return;

	alligator_ht_foreach_arg(hash, labels_free_node, NULL);
	alligator_ht_done(hash);
	free(hash);
}

void labels_hash_insert(alligator_ht *hash, char *name, char *key)
{
	if (!name)
		return;

	uint32_t name_hash = tommy_strhash_u32(0, name);
	labels_container *labelscont = alligator_ht_search(hash, labels_hash_compare, name, name_hash);
	if (!labelscont)
	{
		labelscont = malloc(sizeof(*labelscont));
		labelscont->name = name;
		labelscont->key = key;
		labelscont->allocatedname = 0;
		labelscont->allocatedkey = 0;
		alligator_ht_insert(hash, &(labelscont->node), labelscont, name_hash);
	}
}

void labels_hash_insert_nocache(alligator_ht *hash, char *name, char *key)
{
	if (!name)
		return;

	uint32_t name_hash = tommy_strhash_u32(0, name);
	labels_container *labelscont = alligator_ht_search(hash, labels_hash_compare, name, name_hash);
	if (!labelscont && name && key)
	{
		labelscont = calloc(1, sizeof(*labelscont));
		labelscont->name = strdup(name);
		labelscont->allocatedname = 1;
		labelscont->key = strdup(key);
		labelscont->allocatedkey = 1;
		alligator_ht_insert(hash, &(labelscont->node), labelscont, name_hash);
	}
}

void print_labels(labels_t *labels)
{
	while (labels)
	{
		printf("labels %p, labels->name: '%s' (%zu), labels->key: '%s' (%zu)\n", labels, labels->name, labels->name_len, labels->key, labels->key_len);
		labels = labels->next;
	}
}

namespace_struct *get_ns(char *namespace, namespace_struct *arg_ns)
{
	namespace_struct *ns;

	if ((!namespace) && !(arg_ns))
		ns = ac->nsdefault;
	else if(arg_ns)
		ns = arg_ns;
	else // add support namespaces
		return NULL;

	return ns;
}

void labels_cache_free_foreach(void *funcarg, void* arg)
{
	labels_words_cache *labels_cache = arg;
	
	if (labels_cache->w)
		free(labels_cache->w);

	free(labels_cache);
}

void namespace_free(char *namespace, namespace_struct *arg_ns)
{
	namespace_struct *ns = get_ns(namespace, arg_ns);
	if (!ns)
		return;

	expire_purge(INT64_MAX, namespace);

	alligator_ht_foreach_arg(ns->metrictree->labels_words_hash, labels_cache_free_foreach, NULL);
}

void labels_cache_print_foreach(void *funcarg, void* arg)
{
	labels_words_cache *labels_cache = arg;
	string *str = funcarg;
	
	if (labels_cache->w)
	{
		if (str->l > 2)
			string_cat(str, ",\n", 2);
		string_cat(str, "\"", 1);
		string_cat(str, labels_cache->w, labels_cache->l);
		string_cat(str, "\"", 1);
	}
}

string* namespace_print(char *namespace, namespace_struct *arg_ns)
{
	namespace_struct *ns = get_ns(namespace, arg_ns);
	if (!ns)
		return NULL;

	string *ret = string_new();
	string_cat(ret, "[\n", 2);

	alligator_ht_foreach_arg(ns->metrictree->labels_words_hash, labels_cache_print_foreach, ret);
	string_cat(ret, "]\n", 2);

	return ret;
}

void labels_cache_fill(labels_t *labels, metric_tree *metrictree)
{
	if (!labels)
		return;

	labels_words_cache *labels_cache;
	alligator_ht *labels_words_hash = metrictree->labels_words_hash;

	while (labels)
	{
		uint32_t name_hash = tommy_strhash_u32(0, labels->name);
		labels_cache = alligator_ht_search(labels_words_hash, labels_word_hash_compare, labels->name, name_hash);
		if (labels_cache)
		{
			++ac->metric_cache_hits;
			labels_cache->count++;
		}
		else
		{
			++ac->metric_allocates;
			labels_cache = malloc(sizeof(*labels_cache));
			labels_cache->w = strdup(labels->name);
			labels_cache->l = labels->name_len;
			labels_cache->count = 1;
			alligator_ht_insert(labels_words_hash, &(labels_cache->node), labels_cache, name_hash);
		}

		if (labels->allocatedname)
		{
			//printf("freeing %s\n", labels->name);
			//free(labels->name);
		}

		labels->name = labels_cache->w;

		if(!labels->key)
		{
			labels = labels->next;
			continue;
		}

		uint32_t key_hash = tommy_strhash_u32(0, labels->key);
		labels_cache = alligator_ht_search(labels_words_hash, labels_word_hash_compare, labels->key, key_hash);
		if (labels_cache)
		{
			++ac->metric_cache_hits;
			labels_cache->count++;
		}
		else
		{
			++ac->metric_allocates;
			labels_cache = malloc(sizeof(*labels_cache));
			labels_cache->w = strdup(labels->key);
			labels_cache->l = labels->key_len;
			labels_cache->count = 1;
			alligator_ht_insert(labels_words_hash, &(labels_cache->node), labels_cache, key_hash);
		}

		if (labels->allocatedkey)
		{
			//printf("DEBUG1: free key %p (%s)\n", labels->key, labels->key);
			free(labels->key);
		}

		labels->key = labels_cache->w;

		labels = labels->next;
	}
}

void labels_to_json_for(void *funcarg, void* arg)
{
	labels_container *labelscont = arg;
	json_t *ret = funcarg;

	json_t *key = json_string(labelscont->key);
	json_array_object_insert(ret, labelscont->name, key);
}

json_t *labels_to_json(alligator_ht *labels)
{
	json_t *ret = json_object();
	if (!labels)
		return ret;

	alligator_ht_foreach_arg(labels, labels_to_json_for, ret);

	return ret;
}

void labels_dup_for(void *funcarg, void* arg)
{
	alligator_ht *dst = funcarg;
	labels_container *labelscont = arg;

	labels_hash_insert_nocache(dst, labelscont->name, labelscont->key);
}

alligator_ht *labels_dup(alligator_ht *labels)
{
	alligator_ht *rlbl = alligator_ht_init(NULL);

	alligator_ht_foreach_arg(labels, labels_dup_for, rlbl);

	return rlbl;
}

void metric_update(char *name, alligator_ht *labels, void* value, int8_t type, context_arg *carg)
{
	namespace_struct *ns;

	if (!carg || !carg->namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;


	if (!labels)
	{
		labels = alligator_ht_init(NULL);
	}

	if (carg && carg->labels)
		labels_merge(labels, carg->labels);

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;
	int64_t ttl = get_ttl(carg);

	labels_t *labels_list = labels_initiate(labels, name, NULL, ns, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_gset(mnode, type, value, expiretree, ttl);
		labels_head_free(labels_list);
	}
	else
	{
		mnode = metric_insert(tree, labels_list, type, value, expiretree, ttl);
	}
}

void metric_update_labels2(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2)
{
	namespace_struct *ns;

	if (numbercheck(name))
		return;

	if (!carg || !carg->namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;
	int64_t ttl = get_ttl(carg);

	alligator_ht *hash = alligator_ht_init(NULL);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);

	if (carg && carg->labels)
		labels_merge(hash, carg->labels);

	labels_t *labels_list = labels_initiate(hash, name, 0, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_gset(mnode, type, value, expiretree, ttl);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree, ttl);
	}
}

void metric_update_labels3(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3)
{
	namespace_struct *ns;

	if (numbercheck(name))
		return;

	if (!carg || !carg->namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;
	int64_t ttl = get_ttl(carg);

	alligator_ht *hash = alligator_ht_init(NULL);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);
	labels_hash_insert(hash, name3, key3);

	if (carg && carg->labels)
		labels_merge(hash, carg->labels);

	labels_t *labels_list = labels_initiate(hash, name, 0, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_gset(mnode, type, value, expiretree, ttl);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree, ttl);
	}
}

void metric_add(char *name, alligator_ht *labels, void* value, int8_t type, context_arg *carg)
{
	//r_time start = setrtime();

	if (numbercheck(name))
		return;

	namespace_struct *ns = get_namespace_by_carg(carg);

	if (!labels)
	{
		labels = alligator_ht_init(NULL);
	}

	if (carg && carg->labels)
		labels_merge(labels, carg->labels);

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;
	int64_t ttl = get_ttl(carg);

	labels_t *labels_list = labels_initiate(labels, name, NULL, ns, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree, ttl);
		labels_head_free(labels_list);
	}
	else
	{
		mnode = metric_insert(tree, labels_list, type, value, expiretree, ttl);
	}

	if (carg && carg->mm && !strstr(name, "_quantile") && !strstr(name, "_le") && !strstr(name, "_bucket"))
	{
		mapping_processing(carg, mnode, metric_get_int(value, type));
	}
	//r_time end = setrtime();
	//getrtime(start, end);
}

void metric_add_auto(char *name, void* value, int8_t type, context_arg *carg)
{
	namespace_struct *ns;

	if (numbercheck(name))
		return;

	if (!carg || !carg->namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;
	int64_t ttl = get_ttl(carg);

	alligator_ht *labels = alligator_ht_init(NULL);

	if (carg && carg->labels)
		labels_merge(labels, carg->labels);

	labels_t *labels_list = labels_initiate(labels, name, 0, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree, ttl);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree, ttl);
	}
}

void metric_add_labels(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1)
{
	namespace_struct *ns;

	if (numbercheck(name))
		return;

	if (!carg || !carg->namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;
	int64_t ttl = get_ttl(carg);

	alligator_ht *hash = alligator_ht_init(NULL);

	labels_hash_insert(hash, name1, key1);

	if (carg && carg->labels)
		labels_merge(hash, carg->labels);

	labels_t *labels_list = labels_initiate(hash, name, 0, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree, ttl);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree, ttl);
	}
}

void metric_add_labels2(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2)
{
	namespace_struct *ns;

	if (numbercheck(name))
		return;

	if (!carg || !carg->namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;
	int64_t ttl = get_ttl(carg);

	alligator_ht *hash = alligator_ht_init(NULL);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);

	if (carg && carg->labels)
		labels_merge(hash, carg->labels);

	labels_t *labels_list = labels_initiate(hash, name, 0, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree, ttl);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree, ttl);
	}
}

void metric_add_labels3(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3)
{
	namespace_struct *ns;

	if (numbercheck(name))
		return;

	if (!carg || !carg->namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;
	int64_t ttl = get_ttl(carg);

	alligator_ht *hash = alligator_ht_init(NULL);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);
	labels_hash_insert(hash, name3, key3);

	if (carg && carg->labels)
		labels_merge(hash, carg->labels);

	labels_t *labels_list = labels_initiate(hash, name, 0, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree, ttl);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree, ttl);
	}
}

void metric_add_labels4(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4)
{
	namespace_struct *ns;

	if (numbercheck(name))
		return;

	if (!carg || !carg->namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;
	int64_t ttl = get_ttl(carg);

	alligator_ht *hash = alligator_ht_init(NULL);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);
	labels_hash_insert(hash, name3, key3);
	labels_hash_insert(hash, name4, key4);

	if (carg && carg->labels)
		labels_merge(hash, carg->labels);

	labels_t *labels_list = labels_initiate(hash, name, 0, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree, ttl);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree, ttl);
	}
}

void metric_add_labels5(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5)
{
	namespace_struct *ns;

	if (numbercheck(name))
		return;

	if (!carg || !carg->namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;
	int64_t ttl = get_ttl(carg);

	alligator_ht *hash = alligator_ht_init(NULL);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);
	labels_hash_insert(hash, name3, key3);
	labels_hash_insert(hash, name4, key4);
	labels_hash_insert(hash, name5, key5);

	if (carg && carg->labels)
		labels_merge(hash, carg->labels);

	labels_t *labels_list = labels_initiate(hash, name, 0, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree, ttl);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree, ttl);
	}
}

void metric_add_labels6(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5, char *name6, char *key6)
{
	namespace_struct *ns;

	if (numbercheck(name))
		return;

	if (!carg || !carg->namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;
	int64_t ttl = get_ttl(carg);

	alligator_ht *hash = alligator_ht_init(NULL);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);
	labels_hash_insert(hash, name3, key3);
	labels_hash_insert(hash, name4, key4);
	labels_hash_insert(hash, name5, key5);
	labels_hash_insert(hash, name6, key6);

	if (carg && carg->labels)
		labels_merge(hash, carg->labels);

	labels_t *labels_list = labels_initiate(hash, name, 0, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree, ttl);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree, ttl);
	}
}

void metric_add_labels7(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5, char *name6, char *key6, char *name7, char *key7)
{
	namespace_struct *ns;

	if (numbercheck(name))
		return;

	if (!carg || !carg->namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;
	int64_t ttl = get_ttl(carg);

	alligator_ht *hash = alligator_ht_init(NULL);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);
	labels_hash_insert(hash, name3, key3);
	labels_hash_insert(hash, name4, key4);
	labels_hash_insert(hash, name5, key5);
	labels_hash_insert(hash, name6, key6);
	labels_hash_insert(hash, name7, key7);

	if (carg && carg->labels)
		labels_merge(hash, carg->labels);

	labels_t *labels_list = labels_initiate(hash, name, 0, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree, ttl);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree, ttl);
	}
}

void metric_add_labels8(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5, char *name6, char *key6, char *name7, char *key7, char *name8, char *key8)
{
	namespace_struct *ns;

	if (numbercheck(name))
		return;

	if (!carg || !carg->namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;
	int64_t ttl = get_ttl(carg);

	alligator_ht *hash = alligator_ht_init(NULL);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);
	labels_hash_insert(hash, name3, key3);
	labels_hash_insert(hash, name4, key4);
	labels_hash_insert(hash, name5, key5);
	labels_hash_insert(hash, name6, key6);
	labels_hash_insert(hash, name7, key7);
	labels_hash_insert(hash, name8, key8);

	if (carg && carg->labels)
		labels_merge(hash, carg->labels);

	labels_t *labels_list = labels_initiate(hash, name, 0, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree, ttl);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree, ttl);
	}
}

void metric_add_labels9(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5, char *name6, char *key6, char *name7, char *key7, char *name8, char *key8, char* name9, char* key9)
{
	namespace_struct *ns;

	if (numbercheck(name))
		return;

	if (!carg || !carg->namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;
	int64_t ttl = get_ttl(carg);

	alligator_ht *hash = alligator_ht_init(NULL);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);
	labels_hash_insert(hash, name3, key3);
	labels_hash_insert(hash, name4, key4);
	labels_hash_insert(hash, name5, key5);
	labels_hash_insert(hash, name6, key6);
	labels_hash_insert(hash, name7, key7);
	labels_hash_insert(hash, name8, key8);
	labels_hash_insert(hash, name9, key9);

	if (carg && carg->labels)
		labels_merge(hash, carg->labels);

	labels_t *labels_list = labels_initiate(hash, name, 0, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree, ttl);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree, ttl);
	}
}

void metric_add_labels10(char *name, void* value, int8_t type, context_arg *carg, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5, char *name6, char *key6, char *name7, char *key7, char *name8, char *key8, char* name9, char* key9, char* name10, char* key10)
{
	namespace_struct *ns;

	if (numbercheck(name))
		return;

	if (!carg || !carg->namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;
	int64_t ttl = get_ttl(carg);

	alligator_ht *hash = alligator_ht_init(NULL);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);
	labels_hash_insert(hash, name3, key3);
	labels_hash_insert(hash, name4, key4);
	labels_hash_insert(hash, name5, key5);
	labels_hash_insert(hash, name6, key6);
	labels_hash_insert(hash, name7, key7);
	labels_hash_insert(hash, name8, key8);
	labels_hash_insert(hash, name9, key9);
	labels_hash_insert(hash, name10, key10);

	if (carg && carg->labels)
		labels_merge(hash, carg->labels);

	labels_t *labels_list = labels_initiate(hash, name, 0, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree, ttl);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree, ttl);
	}
}

void metric_gen_foreach_avg(void *funcarg, void* arg)
{
	query_struct *qs = arg;

	query_pass *qp = funcarg;
	char *name = qp->new_name;
	if (ac->log_level > 2)
		printf("qs->key %s, val: %lf, count: %"u64"\n", qs->key, qs->val, qs->count);

	double avg = qs->val / qs->count;
	metric_add(name, qs->lbl, &avg, DATATYPE_DOUBLE, ac->system_carg);

	free(qs->key);
	free(qs);
}

void metric_gen_foreach_free_res(void *funcarg, void* arg)
{
	query_struct *qs = arg;
	free(qs->key);
	free(qs);
}

uint8_t query_struct_check_expr(uint8_t op, double val, double opval)
{
	if (!op)
		return 1;

	if (op == QUERY_OPERATOR_EQ)
		return val == opval;
	else if (op == QUERY_OPERATOR_NE)
		return val != opval;
	else if (op == QUERY_OPERATOR_GT)
		return val > opval;
	else if (op == QUERY_OPERATOR_LT)
		return val < opval;
	else if (op == QUERY_OPERATOR_GE)
		return val >= opval;
	else if (op == QUERY_OPERATOR_LE)
		return val <= opval;

	printf("query_struct_check_expr: unknown OP!!!\n");
	return 1;
}

void metric_gen_foreach_min(void *funcarg, void* arg)
{
	query_struct *qs = arg;

	query_pass *qp = funcarg;
	metric_query_context *mqc = qp->mqc;
	char *name = qp->new_name;
	if (ac->log_level > 2)
		printf("qs->key %s, min: %lf, op: %d, opval: %lf\n", qs->key, qs->min, mqc->op, mqc->opval);

	if (query_struct_check_expr(mqc->op, qs->min, mqc->opval))
	{
		metric_add(name, qs->lbl, &qs->min, DATATYPE_DOUBLE, ac->system_carg);
		action_query_foreach_process(qs, qp->an, &qs->min, DATATYPE_DOUBLE);
		qp->action_need_run = 1;
	}

	free(qs->key);
	free(qs);
}

void metric_gen_foreach_max(void *funcarg, void* arg)
{
	query_struct *qs = arg;

	query_pass *qp = funcarg;
	metric_query_context *mqc = qp->mqc;
	char *name = qp->new_name;
	if (ac->log_level > 2)
		printf("qs->key %s, max: %lf, op: %d, opval: %lf\n", qs->key, qs->max, mqc->op, mqc->opval);

	if (query_struct_check_expr(mqc->op, qs->max, mqc->opval))
	{
		metric_add(name, qs->lbl, &qs->max, DATATYPE_DOUBLE, ac->system_carg);
		action_query_foreach_process(qs, qp->an, &qs->max, DATATYPE_DOUBLE);
		qp->action_need_run = 1;
	}

	free(qs->key);
	free(qs);
}

void metric_gen_foreach_sum(void *funcarg, void* arg)
{
	query_struct *qs = arg;

	query_pass *qp = funcarg;
	metric_query_context *mqc = qp->mqc;
	char *name = qp->new_name;
	if (ac->log_level > 2)
		printf("qs->key %s, val: %lf, op: %d, opval: %lf\n", qs->key, qs->val, mqc->op, mqc->opval);

	if (query_struct_check_expr(mqc->op, qs->val, mqc->opval))
	{
		metric_add(name, qs->lbl, &qs->val, DATATYPE_DOUBLE, ac->system_carg);
		action_query_foreach_process(qs, qp->an, &qs->val, DATATYPE_DOUBLE);
		qp->action_need_run = 1;
	}

	free(qs->key);
	free(qs);
}

void metric_gen_foreach_count(void *funcarg, void* arg)
{
	query_struct *qs = arg;

	query_pass *qp = funcarg;
	metric_query_context *mqc = qp->mqc;
	char *name = qp->new_name;
	if (ac->log_level > 2)
		printf("qs->key %s, count: %"u64", op: %d, opval: %lf\n", qs->key, qs->count, mqc->op, mqc->opval);

	if (query_struct_check_expr(mqc->op, qs->count, mqc->opval))
	{
		metric_add(name, qs->lbl, &qs->count, DATATYPE_UINT, ac->system_carg);
		action_query_foreach_process(qs, qp->an, &qs->count, DATATYPE_UINT);
		qp->action_need_run = 1;
	}

	free(qs->key);
	free(qs);
}

void metric_query_gen (char *namespace, metric_query_context *mqc, char *new_name, char *action_name)
{
	namespace_struct *ns;

	if (!namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;
	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;

	size_t labels_count = alligator_ht_count(mqc->lbl);
	labels_t *labels_list = labels_initiate(mqc->lbl, mqc->name, 0, 0, 0);

	if ( tree && tree->root)
	{
		alligator_ht *res_hash = alligator_ht_init(NULL);
		metrictree_gen(tree->root, labels_list, mqc->groupkey, res_hash, labels_count);
		labels_list->key = new_name;
		labels_list->key_len = strlen(new_name);

		int64_t ttl = get_ttl(NULL);

		uint64_t value = 0;
		double dvalue = 0;
		int type = DATATYPE_UINT;
		uint64_t res_count = alligator_ht_count(res_hash);
		int func = mqc->func;

		action_node *an = NULL;
		if (res_count && action_name)
			an = action_get(action_name);

		if (res_count && (func == QUERY_FUNC_PRESENT))
		{
			value = res_count > 0 ? 1 : 0;
		}
		else if (res_count && (func == QUERY_FUNC_ABSENT))
		{
			value = res_count > 0 ? 0 : 1;
		}
		else if (res_count && (func == QUERY_FUNC_COUNT))
		{
			query_pass qp;
			qp.new_name = new_name;
			qp.an = an;
			qp.mqc = mqc;
			qp.action_need_run = 0;
			alligator_ht_foreach_arg(res_hash, metric_gen_foreach_count, &qp);
			alligator_ht_done(res_hash);
			free(res_hash);
			labels_head_free(labels_list);
			if (qp.action_need_run)
				action_run_process(action_name);
			return;
		}
		else if (res_count && (func == QUERY_FUNC_SUM))
		{
			query_pass qp;
			qp.new_name = new_name;
			qp.an = an;
			qp.mqc = mqc;
			qp.action_need_run = 0;
			alligator_ht_foreach_arg(res_hash, metric_gen_foreach_sum, &qp);
			alligator_ht_done(res_hash);
			free(res_hash);
			labels_head_free(labels_list);
			if (qp.action_need_run)
				action_run_process(action_name);
			return;
		}
		else if (res_count && (func == QUERY_FUNC_MIN))
		{
			query_pass qp;
			qp.new_name = new_name;
			qp.an = an;
			qp.mqc = mqc;
			qp.action_need_run = 0;
			alligator_ht_foreach_arg(res_hash, metric_gen_foreach_min, &qp);
			alligator_ht_done(res_hash);
			free(res_hash);
			labels_head_free(labels_list);
			if (qp.action_need_run)
				action_run_process(action_name);
			return;
		}
		else if (res_count && (func == QUERY_FUNC_MAX))
		{
			query_pass qp;
			qp.new_name = new_name;
			qp.an = an;
			qp.mqc = mqc;
			qp.action_need_run = 0;
			//alligator_ht_foreach_arg(res_hash, metric_gen_foreach_max, new_name);
			alligator_ht_foreach_arg(res_hash, metric_gen_foreach_max, &qp);
			alligator_ht_done(res_hash);
			free(res_hash);
			if (qp.action_need_run)
				action_run_process(action_name);
			labels_head_free(labels_list);
			return;
		}
		else if (res_count && (func == QUERY_FUNC_AVG))
		{
			query_pass qp;
			qp.new_name = new_name;
			qp.an = an;
			qp.mqc = mqc;
			qp.action_need_run = 0;
			alligator_ht_foreach_arg(res_hash, metric_gen_foreach_avg, &qp);
			alligator_ht_done(res_hash);
			free(res_hash);
			labels_head_free(labels_list);
			if (qp.action_need_run)
				action_run_process(action_name);
			return;
		}

		// check expr
		if (!query_struct_check_expr(mqc->op, value, mqc->opval))
		{
			tommy_hash_forfree(res_hash, metric_gen_foreach_free_res);
			//alligator_ht_done(res_hash);
			//free(res_hash);
			labels_head_free(labels_list);
			return;
		}

		metric_node* mnode = metric_find(tree, labels_list);
		if (mnode)
		{
			labels_head_free(labels_list);
			if (type == DATATYPE_UINT)
				metric_set(mnode, type, &value, expiretree, ttl);
			else if (type == DATATYPE_DOUBLE)
				metric_set(mnode, type, &dvalue, expiretree, ttl);
		}
		else
		{
			if (type == DATATYPE_UINT)
				mnode = metric_insert(tree, labels_list, type, &value, expiretree, ttl);
			else if (type == DATATYPE_DOUBLE)
				mnode = metric_insert(tree, labels_list, type, &dvalue, expiretree, ttl);
			else
				labels_head_free(labels_list);
		}

		tommy_hash_forfree(res_hash, metric_gen_foreach_free_res);
		//alligator_ht_done(res_hash);
		//free(res_hash);
	}
}
