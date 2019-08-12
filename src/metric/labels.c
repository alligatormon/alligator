#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "metric/labels.h"
#include "metric/expiretree.h"
#include "dstructures/tommy.h"
#include "metric/namespace.h"
#include "common/mapping.h"
#include "main.h"

int labels_cmp(labels_t *labels1, labels_t *labels2)
{
	extern aconf *ac;
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

	extern aconf *ac;

	labels_words_cache *labels_cache;
	tommy_hashdyn *labels_words_hash = metrictree->labels_words_hash;
	
	while (labels)
	{
		uint32_t name_hash = tommy_strhash_u32(0, labels->name);
		labels_cache = tommy_hashdyn_search(labels_words_hash, labels_word_hash_compare, labels->name, name_hash);
		if (labels_cache)
		{
			labels_cache->count--;
			if (labels_cache->count == 0)
			{
				free(labels_cache->w);
				tommy_hashdyn_remove_existing(labels_words_hash, &(labels_cache->node));
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
			return;
		}
		uint32_t key_hash = tommy_strhash_u32(0, labels->key);
		labels_cache = tommy_hashdyn_search(labels_words_hash, labels_word_hash_compare, labels->key, key_hash);
		if (labels_cache)
		{
			labels_cache->count--;
			if (labels_cache->count == 0)
			{
				free(labels_cache->w);
				tommy_hashdyn_remove_existing(labels_words_hash, &(labels_cache->node));
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
	cur->name = labelscont->name;
	cur->name_len = strlen(cur->name);
	cur->key = labelscont->key;
	cur->key_len = strlen(cur->key);
	cur->next = 0;

	cur->allocatedname = labelscont->allocatedname;
	cur->allocatedkey = labelscont->allocatedkey;

	// add element to sortplan
	sort_plan->plan[(sort_plan->size)++] = labelscont->name;

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
			printf("DEBUG1: free key %p (%s)\n", labels->key, labels->key);
			free(labels->key);
		}
		free(labels);
		labels = new;
	}
}

labels_t* labels_initiate(tommy_hashdyn *hash, char *name, char *namespace, namespace_struct *arg_ns)
{
	extern aconf *ac;
	namespace_struct *ns;

	if ((!namespace) && !(arg_ns))
		ns = ac->nsdefault;
	else if(arg_ns)
		ns = arg_ns;
	else // add support namespaces
		return NULL;

	if (!hash)
	{
		hash = malloc(sizeof(*hash));
		tommy_hashdyn_init(hash);
	}

	labels_t *labels = malloc(sizeof(*labels));
	labels->name = "__name__";
	labels->name_len = 8;
	labels->key = name;
	labels->key_len = strlen(name);
	labels->allocatedname = 0;
	labels->allocatedkey = 0;

	labels->sort_plan = ns->metrictree->sort_plan;
	sortplan *sort_plan = ns->metrictree->sort_plan;

	labels_t *cur = labels;

	uint64_t i;
	for (i=1; i<sort_plan->size; i++)
	{
		cur->next = malloc(sizeof(labels_t));
		cur = cur->next;
		cur->sort_plan = sort_plan;
		cur->name = sort_plan->plan[i];
		cur->name_len = strlen(cur->name);
		labels_container *labelscont = tommy_hashdyn_search(hash, labels_hash_compare, sort_plan->plan[i], tommy_strhash_u32(0, sort_plan->plan[i]));
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
			tommy_hashdyn_remove_existing(hash, &(labelscont->node));

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
	tommy_hashdyn_foreach_arg(hash, labels_new_plan_node, cur);
	tommy_hashdyn_done(hash);
	free(hash);

	return labels;
}

void labels_free_node(void *funcarg, void* arg)
{
	labels_container *labelscont = arg;
	if (!labelscont)
		return;

	if (labelscont->allocatedkey)
	{
		printf("DEBUG1: free key %p (%s)\n", labelscont->key, labelscont->key);
		free(labelscont->key);
	}

	free(labelscont);
}

void labels_hash_free(tommy_hashdyn *hash)
{
	tommy_hashdyn_foreach_arg(hash, labels_free_node, NULL);
	tommy_hashdyn_done(hash);
	free(hash);
}

void labels_hash_insert(tommy_hashdyn *hash, char *name, char *key)
{
	if (!name)
		return;

	uint32_t name_hash = tommy_strhash_u32(0, name);
	labels_container *labelscont = tommy_hashdyn_search(hash, labels_hash_compare, name, name_hash);
	if (!labelscont)
	{
		labelscont = malloc(sizeof(*labelscont));
		labelscont->name = name;
		labelscont->key = key;
		labelscont->allocatedname = 0;
		labelscont->allocatedkey = 0;
		tommy_hashdyn_insert(hash, &(labelscont->node), labelscont, name_hash);
	}
}

void labels_hash_insert_nocache(tommy_hashdyn *hash, char *name, char *key)
{
	if (!name)
		return;

	uint32_t name_hash = tommy_strhash_u32(0, name);
	labels_container *labelscont = tommy_hashdyn_search(hash, labels_hash_compare, name, name_hash);
	if (!labelscont)
	{
		labelscont = malloc(sizeof(*labelscont));
		labelscont->name = strdup(name);
		labelscont->key = strdup(key);
		labelscont->allocatedname = 1;
		labelscont->allocatedkey = 1;
		tommy_hashdyn_insert(hash, &(labelscont->node), labelscont, name_hash);
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

void labels_cache_fill(labels_t *labels, metric_tree *metrictree)
{
	extern aconf *ac;

	if (!labels)
		return;

	labels_words_cache *labels_cache;
	tommy_hashdyn *labels_words_hash = metrictree->labels_words_hash;

	while (labels)
	{
		uint32_t name_hash = tommy_strhash_u32(0, labels->name);
		labels_cache = tommy_hashdyn_search(labels_words_hash, labels_word_hash_compare, labels->name, name_hash);
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
			tommy_hashdyn_insert(labels_words_hash, &(labels_cache->node), labels_cache, name_hash);
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
		labels_cache = tommy_hashdyn_search(labels_words_hash, labels_word_hash_compare, labels->key, key_hash);
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
			tommy_hashdyn_insert(labels_words_hash, &(labels_cache->node), labels_cache, key_hash);
		}

		if (labels->allocatedkey)
		{
			printf("DEBUG1: free key %p (%s)\n", labels->key, labels->key);
			free(labels->key);
		}

		labels->key = labels_cache->w;

		labels = labels->next;
	}
}

void metric_add(char *name, tommy_hashdyn *labels, void* value, int8_t type, char *namespace)
{
	extern aconf *ac;
	namespace_struct *ns;

	if (!namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;

	labels_t *labels_list = labels_initiate(labels, name, NULL, ns);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree);
	}
}

void metric_update(char *name, tommy_hashdyn *labels, void* value, int8_t type, char *namespace)
{
	extern aconf *ac;
	namespace_struct *ns;

	if (!namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;

	labels_t *labels_list = labels_initiate(labels, name, NULL, ns);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_gset(mnode, type, value, expiretree);
		labels_head_free(labels_list);
	}
	else
	{
		mnode = metric_insert(tree, labels_list, type, value, expiretree);
	}
}

void metric_add_ret(char *name, tommy_hashdyn *labels, void* value, int8_t type, char *namespace, mapping_metric *mm)
{
	extern aconf *ac;
	namespace_struct *ns;

	if (!namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;

	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;

	labels_t *labels_list = labels_initiate(labels, name, NULL, ns);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree);
		labels_head_free(labels_list);
	}
	else
	{
		mnode = metric_insert(tree, labels_list, type, value, expiretree);
	}

	if (mm)
		mapping_processing(mm, mnode, metric_get_int(value, type));
}

void metric_add_auto(char *name, void* value, int8_t type, char *namespace)
{
	extern aconf *ac;
	namespace_struct *ns;

	if (!namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;
	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;

	labels_t *labels_list = labels_initiate(NULL, name, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree);
	}
}

void metric_add_labels(char *name, void* value, int8_t type, char *namespace, char *name1, char *key1)
{
	extern aconf *ac;
	namespace_struct *ns;

	if (!namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;
	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;

	tommy_hashdyn *hash = malloc(sizeof(*hash));
	tommy_hashdyn_init(hash);

	labels_hash_insert(hash, name1, key1);
	labels_t *labels_list = labels_initiate(hash, name, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree);
	}
}

void metric_add_labels2(char *name, void* value, int8_t type, char *namespace, char *name1, char *key1, char *name2, char *key2)
{
	extern aconf *ac;
	namespace_struct *ns;

	if (!namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;
	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;

	tommy_hashdyn *hash = malloc(sizeof(*hash));
	tommy_hashdyn_init(hash);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);
	labels_t *labels_list = labels_initiate(hash, name, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree);
	}
}

void metric_add_labels3(char *name, void* value, int8_t type, char *namespace, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3)
{
	extern aconf *ac;
	namespace_struct *ns;

	if (!namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;
	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;

	tommy_hashdyn *hash = malloc(sizeof(*hash));
	tommy_hashdyn_init(hash);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);
	labels_hash_insert(hash, name3, key3);
	labels_t *labels_list = labels_initiate(hash, name, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree);
	}
}

void metric_add_labels4(char *name, void* value, int8_t type, char *namespace, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4)
{
	extern aconf *ac;
	namespace_struct *ns;

	if (!namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;
	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;

	tommy_hashdyn *hash = malloc(sizeof(*hash));
	tommy_hashdyn_init(hash);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);
	labels_hash_insert(hash, name3, key3);
	labels_hash_insert(hash, name4, key4);
	labels_t *labels_list = labels_initiate(hash, name, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree);
	}
}

void metric_add_labels5(char *name, void* value, int8_t type, char *namespace, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5)
{
	extern aconf *ac;
	namespace_struct *ns;

	if (!namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;
	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;

	tommy_hashdyn *hash = malloc(sizeof(*hash));
	tommy_hashdyn_init(hash);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);
	labels_hash_insert(hash, name3, key3);
	labels_hash_insert(hash, name4, key4);
	labels_hash_insert(hash, name5, key5);
	labels_t *labels_list = labels_initiate(hash, name, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree);
	}
}

void metric_add_labels6(char *name, void* value, int8_t type, char *namespace, char *name1, char *key1, char *name2, char *key2, char *name3, char *key3, char *name4, char *key4, char *name5, char *key5, char *name6, char *key6)
{
	extern aconf *ac;
	namespace_struct *ns;

	if (!namespace)
		ns = ac->nsdefault;
	else // add support namespaces
		return;
	metric_tree *tree = ns->metrictree;
	expire_tree *expiretree = ns->expiretree;

	tommy_hashdyn *hash = malloc(sizeof(*hash));
	tommy_hashdyn_init(hash);

	labels_hash_insert(hash, name1, key1);
	labels_hash_insert(hash, name2, key2);
	labels_hash_insert(hash, name3, key3);
	labels_hash_insert(hash, name4, key4);
	labels_hash_insert(hash, name5, key5);
	labels_hash_insert(hash, name6, key6);
	labels_t *labels_list = labels_initiate(hash, name, 0, 0);
	metric_node* mnode = metric_find(tree, labels_list);
	if (mnode)
	{
		metric_set(mnode, type, value, expiretree);
		labels_head_free(labels_list);
	}
	else
	{
		metric_insert(tree, labels_list, type, value, expiretree);
	}
}
