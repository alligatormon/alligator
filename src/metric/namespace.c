#include "main.h"
#include "common/logs.h"
#include "metric/metric_types.h"
extern aconf *ac;

int metric_family_metadata_compare(const void *arg, const void *obj)
{
	const char *s1 = arg;
	const char *s2 = ((metric_family_metadata *)obj)->key;
	return strcmp(s1, s2);
}

void metric_family_metadata_free_foreach(void *funcarg, void *arg)
{
	(void)funcarg;
	metric_family_metadata *meta = arg;
	free(meta->key);
	if (meta->help)
		free(meta->help);
	free(meta);
}

metric_family_metadata *namespace_metric_family_get(namespace_struct *ns, const char *metric_name)
{
	if (!ns || !ns->metric_families || !metric_name)
		return NULL;

	uint32_t key_hash = tommy_strhash_u32(0, metric_name);
	return alligator_ht_search_nolock(ns->metric_families, metric_family_metadata_compare, metric_name, key_hash);
}

void namespace_metric_family_set(char *namespace, context_arg *carg, const char *metric_name, uint8_t type, const char *help)
{
	if (!metric_name || !metric_name[0])
		return;

	namespace_struct *ns = namespace ? get_namespace_or_null(namespace) : get_namespace_by_carg(carg);
	if (!ns || !ns->metric_families || !ns->metrictree || !ns->metrictree->rwlock)
		return;

	pthread_rwlock_wrlock(ns->metrictree->rwlock);
	uint32_t key_hash = tommy_strhash_u32(0, metric_name);
	metric_family_metadata *meta = alligator_ht_search_nolock(ns->metric_families, metric_family_metadata_compare, metric_name, key_hash);
	if (!meta)
	{
		meta = calloc(1, sizeof(*meta));
		meta->key = strdup(metric_name);
		meta->type = METRIC_TYPE_UNTYPED;
		alligator_ht_insert_nolock(ns->metric_families, &(meta->node), meta, key_hash);
	}

	if (meta)
	{
		if (type != UINT8_MAX)
			meta->type = type;

		if (help)
		{
			char *new_help = strdup(help);
			if (new_help)
			{
				if (meta->help)
					free(meta->help);
				meta->help = new_help;
			}
		}
	}
	pthread_rwlock_unlock(ns->metrictree->rwlock);
}

static int prom_family_ends_with(const char *name, const char *suffix, size_t *base_len)
{
	size_t name_len = strlen(name);
	size_t suffix_len = strlen(suffix);
	if (name_len <= suffix_len)
		return 0;
	if (strcmp(name + name_len - suffix_len, suffix))
		return 0;
	*base_len = name_len - suffix_len;
	return 1;
}

int prom_family_strip_histogram_suffix(const char *metric_name, char *base_out, size_t base_out_size)
{
	static const char *const suffixes[] = {"_bucket", "_count", "_sum"};
	if (!metric_name || !base_out || base_out_size == 0)
		return 0;

	for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
	{
		size_t base_len = 0;
		if (!prom_family_ends_with(metric_name, suffixes[i], &base_len))
			continue;
		if (base_len >= base_out_size)
			return 0;
		memcpy(base_out, metric_name, base_len);
		base_out[base_len] = '\0';
		return 1;
	}
	return 0;
}

int prom_family_strip_summary_suffix(const char *metric_name, char *base_out, size_t base_out_size)
{
	static const char *const suffixes[] = {"_quantile", "_count", "_sum"};
	if (!metric_name || !base_out || base_out_size == 0)
		return 0;

	for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
	{
		size_t base_len = 0;
		if (!prom_family_ends_with(metric_name, suffixes[i], &base_len))
			continue;
		if (base_len >= base_out_size)
			return 0;
		memcpy(base_out, metric_name, base_len);
		base_out[base_len] = '\0';
		return 1;
	}
	return 0;
}

const char *prom_family_exposition_resolve(namespace_struct *ns, const char *metric_name,
    metric_family_metadata **meta_out, char *buf, size_t buf_size)
{
	char base[256];

	if (meta_out)
		*meta_out = NULL;

	if (!metric_name)
		return metric_name;

	if (prom_family_strip_histogram_suffix(metric_name, base, sizeof(base)))
	{
		metric_family_metadata *base_meta = namespace_metric_family_get(ns, base);
		if (base_meta && base_meta->type == METRIC_TYPE_HISTOGRAM && buf && buf_size)
		{
			if (meta_out)
				*meta_out = base_meta;
			strlcpy(buf, base, buf_size);
			return buf;
		}
	}

	if (prom_family_strip_summary_suffix(metric_name, base, sizeof(base)))
	{
		metric_family_metadata *base_meta = namespace_metric_family_get(ns, base);
		if (base_meta && base_meta->type == METRIC_TYPE_SUMMARY && buf && buf_size)
		{
			if (meta_out)
				*meta_out = base_meta;
			strlcpy(buf, base, buf_size);
			return buf;
		}
	}

	if (meta_out)
		*meta_out = namespace_metric_family_get(ns, metric_name);
	return metric_name;
}

static void namespace_metric_family_set_histogram_suffixes(context_arg *carg, const char *metric_name, uint8_t metric_type)
{
	char bucket_name[1024];
	char sum_name[1024];
	char count_name[1024];

	snprintf(bucket_name, sizeof(bucket_name), "%s_bucket", metric_name);
	namespace_metric_family_set(NULL, carg, bucket_name, metric_type, NULL);

	snprintf(sum_name, sizeof(sum_name), "%s_sum", metric_name);
	namespace_metric_family_set(NULL, carg, sum_name, metric_type, NULL);

	snprintf(count_name, sizeof(count_name), "%s_count", metric_name);
	namespace_metric_family_set(NULL, carg, count_name, metric_type, NULL);
}

void namespace_metric_family_set_prom_type(context_arg *carg, const char *metric_name, uint8_t metric_type)
{
	char base[1024];

	if (!metric_name || !metric_name[0])
		return;

	if (metric_type == METRIC_TYPE_HISTOGRAM)
	{
		if (prom_family_strip_histogram_suffix(metric_name, base, sizeof(base)))
		{
			namespace_metric_family_set(NULL, carg, base, METRIC_TYPE_HISTOGRAM, NULL);
			return;
		}
		namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_HISTOGRAM, NULL);
		namespace_metric_family_set_histogram_suffixes(carg, metric_name, METRIC_TYPE_HISTOGRAM);
		return;
	}

	if (metric_type == METRIC_TYPE_SUMMARY)
	{
		if (prom_family_strip_summary_suffix(metric_name, base, sizeof(base)))
		{
			namespace_metric_family_set(NULL, carg, base, METRIC_TYPE_SUMMARY, NULL);
			return;
		}
		namespace_metric_family_set(NULL, carg, metric_name, METRIC_TYPE_SUMMARY, NULL);
		snprintf(base, sizeof(base), "%s_sum", metric_name);
		namespace_metric_family_set(NULL, carg, base, METRIC_TYPE_SUMMARY, NULL);
		snprintf(base, sizeof(base), "%s_count", metric_name);
		namespace_metric_family_set(NULL, carg, base, METRIC_TYPE_SUMMARY, NULL);
		return;
	}

	namespace_metric_family_set(NULL, carg, metric_name, metric_type, NULL);
}

void namespace_metric_family_set_prom_help(context_arg *carg, const char *metric_name, const char *help)
{
	char base[1024];

	if (!metric_name || !metric_name[0] || !help)
		return;

	if (prom_family_strip_histogram_suffix(metric_name, base, sizeof(base)))
	{
		namespace_metric_family_set(NULL, carg, base, UINT8_MAX, help);
		return;
	}

	if (prom_family_strip_summary_suffix(metric_name, base, sizeof(base)))
	{
		namespace_metric_family_set(NULL, carg, base, UINT8_MAX, help);
		return;
	}

	namespace_metric_family_set(NULL, carg, metric_name, UINT8_MAX, help);
}

int namespace_struct_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((namespace_struct*)obj)->key;
        return strcmp(s1, s2);
}

namespace_struct *insert_namespace(char *key, uint64_t max_emit)
{
	if (!key)
		return NULL;

	uint32_t key_hash = tommy_strhash_u32(0, key);
	namespace_struct *ns = alligator_ht_search(ac->_namespace, namespace_struct_compare, key, key_hash);
	if (max_emit)
		ns->max_emit = max_emit;
	if (ns)
		return ns;

	ns = calloc(1, sizeof(*ns));
	ns->key = strdup(key);
	ns->max_emit_lock = calloc(1, sizeof(*ns->max_emit_lock));
	ns->metric_families = alligator_ht_init(NULL);
	if (ns->max_emit_lock)
		pthread_mutex_init(ns->max_emit_lock, NULL);
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
	sort_plan->len[0] = MAIN_METRIC_LEN;

	sort_plan->check_collisions = alligator_ht_init(NULL);
	sortplan_collision *sp_colls = malloc(sizeof(*sp_colls));
	sp_colls->index = 0;
	sp_colls->name = sort_plan->plan[0];
	alligator_ht_insert(sort_plan->check_collisions, &(sp_colls->node), sp_colls, sort_plan->hash[0]);

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

void sortplan_collision_foreach_free(void *funcarg, void *arg) {
	sortplan_collision *sp_colls = arg;
	free(sp_colls);
}

void namespaces_free_foreach(void *funcarg, void* arg)
{
	namespace_struct *ns = arg;

	pthread_rwlock_destroy(ns->metrictree->rwlock);
	pthread_rwlock_destroy(ns->expiretree->rwlock);
	if (ns->max_emit_lock) {
		pthread_mutex_destroy(ns->max_emit_lock);
		free(ns->max_emit_lock);
	}
	free(ns->key);
	free(ns->expiretree->rwlock);
	free(ns->expiretree);

	if (ns->metric_families)
	{
		alligator_ht_foreach_arg(ns->metric_families, metric_family_metadata_free_foreach, NULL);
		alligator_ht_done(ns->metric_families);
		free(ns->metric_families);
	}

	alligator_ht_foreach_arg(ns->metrictree->sort_plan->check_collisions, sortplan_collision_foreach_free, NULL);
	alligator_ht_done(ns->metrictree->sort_plan->check_collisions);
	free(ns->metrictree->sort_plan->check_collisions);

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
	ns->max_emit_lock = calloc(1, sizeof(*ns->max_emit_lock));
	ns->metric_families = alligator_ht_init(NULL);
	if (ns->max_emit_lock)
		pthread_mutex_init(ns->max_emit_lock, NULL);
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
	sort_plan->len[0] = MAIN_METRIC_LEN;

	sort_plan->check_collisions = alligator_ht_init(NULL);
	sortplan_collision *sp_colls = malloc(sizeof(*sp_colls));
	sp_colls->index = 0;
	sp_colls->name = sort_plan->plan[0];
	alligator_ht_insert(sort_plan->check_collisions, &(sp_colls->node), sp_colls, sort_plan->hash[0]);

	sort_plan->size = 1;

	ns->metrictree = metrictree;
	ns->expiretree = expiretree;
	metrictree->labels_words_hash = labels_words_hash;
	metrictree->sort_plan = sort_plan;

	/* Daemon sets this in aggregate_ctx_init(); tests need it before parser_push(). */
	if (!ac->aggregate_ctx)
		ac->aggregate_ctx = alligator_ht_init(NULL);
}
