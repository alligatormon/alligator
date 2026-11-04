#include "grok/type.h"
#include "common/logs.h"
#include "common/selector.h"
#include "mapping/type.h"
#include "main.h"
#include <unistd.h>
extern aconf *ac;

void grok_node_del(grok_node *gn)
{
	if (!gn)
		return;
	if (gn->name)
		free(gn->name);
	if (gn->value)
		free(gn->value);
	if (gn->match)
		string_free(gn->match);
	if (gn->pcre_extra)
		pcre_free_study(gn->pcre_extra);
	if (gn->reg)
		pcre_free(gn->reg);
	if (gn->expanded_match)
		string_free(gn->expanded_match);
	if (gn->labels)
		labels_hash_free(gn->labels);

	free(gn);
}

void grok_node_foreach_done(void *funcarg, void* arg)
{
	(void)funcarg;
	grok_node_del(arg);
}

static void grok_multimetric_free_node(void *arg)
{
	grok_multimetric_node *n = arg;
	if (!n)
		return;
	free(n->key);
	if (n->metric_name)
		string_free(n->metric_name);
	free(n);
}

static void grok_ht_free_nodes(alligator_ht **hp)
{
	if (!hp || !*hp)
		return;
	/* alligator_ht_forfree frees tommy_hashdyn inside; free the wrapper only. */
	alligator_ht_forfree(*hp, grok_multimetric_free_node);
	free(*hp);
	*hp = NULL;
}

void grok_ds_free_multimetrics(grok_ds *gps)
{
	if (!gps)
		return;
	grok_ht_free_nodes(&gps->gmm_quantile);
	grok_ht_free_nodes(&gps->gmm_bucket);
	grok_ht_free_nodes(&gps->gmm_le);
	grok_ht_free_nodes(&gps->gmm_counter);
	grok_ht_free_nodes(&gps->gmm_gauge);
	grok_ht_free_nodes(&gps->gmm_splited_quantile);
	grok_ht_free_nodes(&gps->gmm_splited_bucket);
	grok_ht_free_nodes(&gps->gmm_splited_le);
	grok_ht_free_nodes(&gps->gmm_splited_counter);
	grok_ht_free_nodes(&gps->gmm_splited_tags);
	grok_ht_free_nodes(&gps->gmm_splited_inherit_tag);
	if (gps->mm) {
		mapping_free_recurse(gps->mm);
		gps->mm = NULL;
	}
}

void grok_ds_destroy(grok_ds *gps)
{
	if (!gps)
		return;
	if (gps->hash) {
		alligator_ht_foreach_arg(gps->hash, grok_node_foreach_done, NULL);
		alligator_ht_done(gps->hash);
		free(gps->hash);
	}
	if (gps->separator)
		string_free(gps->separator);
	grok_ds_free_multimetrics(gps);
	pthread_rwlock_destroy(&gps->lock);
	free(gps->key);
	free(gps);
}

int grok_del(json_t *grok)
{
	json_t *jname = json_object_get(grok, "name");
	if (!jname)
		jname = json_object_get(grok, "make");
	if (!jname || !json_is_string(jname) || !json_string_value(jname)) {
		glog(L_INFO, "grok_del: not specified 'name' (or legacy 'make') option\n");
		return 0;
	}
	char *name = (char *)json_string_value(jname);

	json_t *jkey = json_object_get(grok, "key");
	if (!jkey || !json_is_string(jkey) || !json_string_value(jkey)) {
		glog(L_INFO, "grok_del: not specified 'key' option in '%s'\n", name);
		return 0;
	}
	char *key = (char *)json_string_value(jkey);

	grok_life_lock();
	grok_ds *gps = alligator_ht_search(ac->grok, grokds_compare, key, tommy_strhash_u32(0, key));
	if (!gps || gps->zombie) {
		grok_life_unlock();
		return 1;
	}
	__atomic_fetch_add(&gps->refs, 1, __ATOMIC_ACQ_REL);
	grok_life_unlock();

	pthread_rwlock_wrlock(&gps->lock);
	grok_node *gn = alligator_ht_search(gps->hash, grok_compare, name, tommy_strhash_u32(0, name));
	if (gn)
	{
		alligator_ht_remove_existing(gps->hash, &(gn->node));
		grok_node_del(gn);
	}
	int empty = !alligator_ht_count(gps->hash);
	pthread_rwlock_unlock(&gps->lock);

	if (empty) {
		/* Re-check under life + wrlock so a concurrent push cannot lose a new rule. */
		grok_life_lock();
		pthread_rwlock_wrlock(&gps->lock);
		if (!gps->zombie && !alligator_ht_count(gps->hash)) {
			alligator_ht_remove_existing(ac->grok, &(gps->node));
			gps->zombie = 1;
		}
		pthread_rwlock_unlock(&gps->lock);
		grok_life_unlock();
	}
	grok_put(gps);

	return 1;
}

typedef struct grok_stop_collect {
	grok_ds **list;
	size_t n;
	size_t cap;
} grok_stop_collect;

static void grok_stop_collect_cb(void *funcarg, void *arg)
{
	grok_stop_collect *col = funcarg;
	grok_ds *gps = arg;
	if (!col || !gps)
		return;
	if (col->n >= col->cap) {
		size_t ncap = col->cap ? col->cap * 2 : 8;
		grok_ds **nl = realloc(col->list, ncap * sizeof(*nl));
		if (!nl)
			return;
		col->list = nl;
		col->cap = ncap;
	}
	col->list[col->n++] = gps;
}

void grok_stop()
{
	if (ac->grok_patterns_path)
		string_tokens_free(ac->grok_patterns_path);
	ac->grok_patterns_path = NULL;

	/* Unpin contexts from the table first; destroy only after refs drain so
	 * in-flight grok_handler pins cannot UAF. */
	grok_stop_collect col = {0};
	alligator_ht *tab = NULL;
	grok_life_lock();
	tab = ac->grok;
	ac->grok = NULL;
	if (tab) {
		alligator_ht_foreach_arg(tab, grok_stop_collect_cb, &col);
		for (size_t i = 0; i < col.n; ++i) {
			alligator_ht_remove_existing(tab, &(col.list[i]->node));
			col.list[i]->zombie = 1;
		}
	}
	grok_life_unlock();

	if (tab) {
		alligator_ht_done(tab);
		free(tab);
	}

	int stop_leaked = 0;
	for (size_t i = 0; i < col.n; ++i) {
		grok_ds *gps = col.list[i];
		unsigned spins = 0;
		while (__atomic_load_n(&gps->refs, __ATOMIC_ACQUIRE) > 0) {
			if (++spins > 100000) {
				glog(L_ERROR, "grok_stop: timed out waiting for refs on key '%s' (refs=%d); leaking until exit\n",
					gps->key ? gps->key : "?", __atomic_load_n(&gps->refs, __ATOMIC_RELAXED));
				stop_leaked = 1;
				break;
			}
			usleep(100);
		}
		/* Never destroy while refs>0 — that UAF in-flight handlers. */
		if (__atomic_load_n(&gps->refs, __ATOMIC_ACQUIRE) > 0)
			continue;
		if (!__atomic_exchange_n(&gps->destroy_claimed, 1, __ATOMIC_ACQ_REL))
			grok_ds_destroy(gps);
	}
	free(col.list);

	/* Patterns only after handlers drained (expand runs under handler pin).
	 * If any pin timed out, leave patterns leaked too — safer than UAF. */
	if (stop_leaked) {
		glog(L_ERROR, "grok_stop: skipped pattern free due to drained-ref timeout\n");
		return;
	}
	grok_pattern *gp = __atomic_exchange_n(&ac->grok_patterns, NULL, __ATOMIC_ACQ_REL);
	if (gp) {
		if (gp->nodes)
			free(gp->nodes);
		free(gp);
	}
	grok_patterns_free_retired();
}
