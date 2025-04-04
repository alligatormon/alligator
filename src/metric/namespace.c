#include "main.h"
#include "common/logs.h"
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
	metrictree->rwlock = calloc(1, sizeof(pthread_rwlock_t));
	pthread_rwlock_init(metrictree->rwlock, NULL);
	expire_tree *expiretree = calloc(1, sizeof(*expiretree));
	expiretree->rwlock = calloc(1, sizeof(pthread_rwlock_t));
	pthread_rwlock_init(expiretree->rwlock, NULL);

	alligator_ht* labels_words_hash = alligator_ht_init(NULL);

	sortplan *sort_plan = calloc(1, sizeof(*sort_plan));
	sort_plan->plan[0] = MAIN_METRIC_NAME;
	sort_plan->hash[0] = MAIN_METRIC_HASH;

	sort_plan->check_collissions = alligator_ht_init(NULL);
	sortplan_collission *sp_colls = malloc(sizeof(*sp_colls));
	sp_colls->index = 0;
	alligator_ht_insert(sort_plan->check_collissions, &(sp_colls->node), sp_colls, sort_plan->hash[0]);

	sort_plan->size = 1;

	ns->metrictree = metrictree;
	ns->expiretree = expiretree;
	metrictree->labels_words_hash = labels_words_hash;
	metrictree->sort_plan = sort_plan;
	glog(L_INFO, "inserted namespace %s", key);

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

namespace_struct *get_namespace_or_null(char *key)
{
	if (!key)
		return ac->nsdefault;

	uint32_t key_hash = tommy_strhash_u32(0, key);
	return alligator_ht_search(ac->_namespace, namespace_struct_compare, key, key_hash);
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

void sortplan_collission_foreach_free(void *funcarg, void *arg) {
	sortplan_collission *sp_colls = arg;
	free(sp_colls);
}

void namespaces_free_foreach(void *funcarg, void* arg)
{
	namespace_struct *ns = arg;

	pthread_rwlock_destroy(ns->metrictree->rwlock);
	pthread_rwlock_destroy(ns->expiretree->rwlock);
	free(ns->key);
	free(ns->expiretree->rwlock);
	free(ns->expiretree);

	alligator_ht_foreach_arg(ns->metrictree->sort_plan->check_collissions, sortplan_collission_foreach_free, NULL);
	alligator_ht_done(ns->metrictree->sort_plan->check_collissions);
	free(ns->metrictree->sort_plan->check_collissions);

	free(ns->metrictree->sort_plan);
	alligator_ht_done(ns->metrictree->labels_words_hash);
	free(ns->metrictree->labels_words_hash);
	free(ns->metrictree->rwlock);
	free(ns->metrictree);

	free(ns);
}

void free_namespaces()
{
	alligator_ht_foreach_arg(ac->_namespace, namespaces_free_foreach, NULL);
	alligator_ht_done(ac->_namespace);
	free(ac->_namespace);
}

void ts_initialize()
{
	// initialize multi NS
	extern aconf *ac;
	ac->_namespace = calloc(1, sizeof(alligator_ht));
	alligator_ht_init(ac->_namespace);

	// initialize default NS
	namespace_struct *ns = calloc(1, sizeof(*ns));
	ns->key = strdup("default");
	alligator_ht_insert(ac->_namespace, &(ns->node), ns, tommy_strhash_u32(0, ns->key));
	ac->nsdefault = ns;

	metric_tree *metrictree = calloc(1, sizeof(*metrictree));
	metrictree->rwlock = calloc(1, sizeof(pthread_rwlock_t));
	pthread_rwlock_init(metrictree->rwlock, NULL);
	expire_tree *expiretree = calloc(1, sizeof(*expiretree));
	expiretree->rwlock = calloc(1, sizeof(pthread_rwlock_t));
	pthread_rwlock_init(expiretree->rwlock, NULL);
	
	alligator_ht* labels_words_hash = alligator_ht_init(NULL);

	sortplan *sort_plan = calloc(1, sizeof(*sort_plan));
	sort_plan->plan[0] = MAIN_METRIC_NAME;
	sort_plan->hash[0] = MAIN_METRIC_HASH;

	sort_plan->check_collissions = alligator_ht_init(NULL);
	sortplan_collission *sp_colls = malloc(sizeof(*sp_colls));
	sp_colls->index = 0;
	alligator_ht_insert(sort_plan->check_collissions, &(sp_colls->node), sp_colls, sort_plan->hash[0]);

	sort_plan->size = 1;

	ns->metrictree = metrictree;
	ns->expiretree = expiretree;
	metrictree->labels_words_hash = labels_words_hash;
	metrictree->sort_plan = sort_plan;
}
