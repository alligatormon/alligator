#include "main.h"
extern aconf *ac;

int namespace_struct_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((namespace_struct*)obj)->key;
        return strcmp(s1, s2);
}

namespace_struct *insert_namespace(char *key)
{
	if (!key)
		return NULL;

	uint32_t key_hash = tommy_strhash_u32(0, key);
	namespace_struct *ns = alligator_ht_search(ac->_namespace, namespace_struct_compare, key, key_hash);
	if (ns)
		return ns;

	ns = calloc(1, sizeof(*ns));
	ns->key = strdup(key);

	alligator_ht_insert(ac->_namespace, &(ns->node), ns, tommy_strhash_u32(0, ns->key));

	metric_tree *metrictree = calloc(1, sizeof(*metrictree));
	expire_tree *expiretree = calloc(1, sizeof(*expiretree));

	alligator_ht* labels_words_hash = alligator_ht_init(NULL);

	sortplan *sort_plan = malloc(sizeof(*sort_plan));
	sort_plan->plan[0] = "__name__";
	sort_plan->size = 1;

	ns->metrictree = metrictree;
	ns->expiretree = expiretree;
	metrictree->labels_words_hash = labels_words_hash;
	metrictree->sort_plan = sort_plan;

	return ns;
}

namespace_struct *get_namespace(char *key)
{
	if (!key)
		return ac->nsdefault;

	uint32_t key_hash = tommy_strhash_u32(0, key);
	namespace_struct *ns = alligator_ht_search(ac->_namespace, namespace_struct_compare, key, key_hash);
	if (!ns)
		ns = ac->nsdefault;

	return ns;
}

namespace_struct *get_namespace_by_carg(context_arg *carg)
{
	if (!carg || !carg->namespace)
		return ac->nsdefault;

	char *key = carg->namespace;

	uint32_t key_hash = tommy_strhash_u32(0, key);
	namespace_struct *ns = alligator_ht_search(ac->_namespace, namespace_struct_compare, key, key_hash);
	if (!ns)
		ns = ac->nsdefault;

	return ns;
}

void namespaces_free_foreach(void *funcarg, void* arg)
{
	namespace_struct *ns = arg;

	free(ns->key);
	free(ns->expiretree);
	free(ns->metrictree->sort_plan);
	alligator_ht_done(ns->metrictree->labels_words_hash);
	free(ns->metrictree->labels_words_hash);
	free(ns->metrictree);

	free(ns);
}

void free_namespaces()
{
	printf("free namspace size %zu\n", alligator_ht_count(ac->_namespace));
	alligator_ht_foreach_arg(ac->_namespace, namespaces_free_foreach, NULL);
	free(ac->_namespace);
}
