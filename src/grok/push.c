#include "grok/type.h"
#include "common/logs.h"
#include "common/selector.h"
#include "mapping/type.h"
#include "main.h"
#include <strings.h>
extern aconf *ac;

static void grok_set_separator(grok_ds *gps, json_t *jseparator)
{
	if (!gps || !jseparator || !json_is_string(jseparator))
		return;
	if (gps->separator)
		string_free(gps->separator);
	gps->separator = string_init_dupn((char *)json_string_value(jseparator), json_string_length(jseparator));
}

void grok_parse_multimetrics(grok_ds *gps, json_t *grok)
{
	mapping_metric *mm = NULL;

	json_t *counters = json_object_get(grok, "counter");
	uint64_t counters_size = json_array_size(counters);
	for (uint64_t j = 0; j < counters_size; j++)
	{
		json_t *counter = json_array_get(counters, j);
		if (!counter)
			continue;
		uint64_t counter_size = json_array_size(counter);
		if (counter_size < 2)
			continue;
		json_t *jname = json_array_get(counter, 0);
		json_t *jkey = json_array_get(counter, 1);
		if (!json_is_string(jname) || !json_is_string(jkey))
			continue;
		grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
		gmm_node->metric_name = string_init_dupn((char *)json_string_value(jname), json_string_length(jname));
		gmm_node->key = strdup((char *)json_string_value(jkey));
		if (!gps->gmm_counter)
			gps->gmm_counter = alligator_ht_init(NULL);
		alligator_ht_insert(gps->gmm_counter, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));
	}

	json_t *gauges = json_object_get(grok, "gauge");
	uint64_t gauges_size = json_array_size(gauges);
	for (uint64_t j = 0; j < gauges_size; j++)
	{
		json_t *gauge = json_array_get(gauges, j);
		if (!gauge)
			continue;
		uint64_t gauge_size = json_array_size(gauge);
		if (gauge_size < 2)
			continue;
		json_t *jname = json_array_get(gauge, 0);
		json_t *jkey = json_array_get(gauge, 1);
		if (!json_is_string(jname) || !json_is_string(jkey))
			continue;
		grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
		gmm_node->metric_name = string_init_dupn((char *)json_string_value(jname), json_string_length(jname));
		gmm_node->key = strdup((char *)json_string_value(jkey));
		if (!gps->gmm_gauge)
			gps->gmm_gauge = alligator_ht_init(NULL);
		alligator_ht_insert(gps->gmm_gauge, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));
	}

	json_t *quantiles = json_object_get(grok, "quantiles");
	uint64_t quantiles_size = json_array_size(quantiles);
	for (uint64_t j = 0; j < quantiles_size; j++)
	{
		json_t *quantile = json_array_get(quantiles, j);
		mm = NULL;
		if (!quantile)
			continue;
		uint64_t percentiles_size = json_array_size(quantile);
		if (percentiles_size <= 2)
			continue;

		mm = calloc(1, sizeof(*mm));
		mm->percentile_buffer_min = -1;
		mm->percentile_calc_every = -1;
		mm->quantile_window = -1;
		mm->quantile_window_empty = -1;

		json_t *jname = json_array_get(quantile, 0);
		json_t *jtemplate = json_array_get(quantile, 1);
		if (!json_is_string(jname) || !json_is_string(jtemplate)) {
			free(mm);
			continue;
		}
		/* mapping matches on metric name (exact); field name lives in gmm_node->key */
		mm->metric_name = strdup((char *)json_string_value(jname));
		mm->template = strdup((char *)json_string_value(jname));
		grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
		gmm_node->key = strdup((char *)json_string_value(jtemplate));
		gmm_node->metric_name = string_init_dupn((char *)json_string_value(jname), json_string_length(jname));
		if (!gps->gmm_quantile)
			gps->gmm_quantile = alligator_ht_init(NULL);
		alligator_ht_insert(gps->gmm_quantile, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));

		int64_t *percentiles = calloc(percentiles_size - 2, sizeof(int64_t));
		for (uint64_t i = 2; i < percentiles_size; i++)
			percentiles[i - 2] = mapping_quantile_from_json(json_array_get(quantile, i));

		mm->percentile = percentiles;
		mm->percentile_size = percentiles_size - 2;

		if (!gps->mm)
			gps->mm = mm;
		else
			push_mapping_metric(gps->mm, mm);
	}

	json_t *les = json_object_get(grok, "bucket");
	uint64_t les_outer = json_array_size(les);
	for (uint64_t j = 0; j < les_outer; j++)
	{
		json_t *le = json_array_get(les, j);
		mm = NULL;
		if (!le)
			continue;
		uint64_t le_size = json_array_size(le);
		if (le_size <= 2)
			continue;

		mm = calloc(1, sizeof(*mm));
		json_t *jname = json_array_get(le, 0);
		json_t *jtemplate = json_array_get(le, 1);
		if (!json_is_string(jname) || !json_is_string(jtemplate)) {
			free(mm);
			continue;
		}
		mm->metric_name = strdup((char *)json_string_value(jname));
		mm->template = strdup((char *)json_string_value(jname));
		grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
		gmm_node->key = strdup((char *)json_string_value(jtemplate));
		gmm_node->metric_name = string_init_dupn((char *)json_string_value(jname), json_string_length(jname));
		if (!gps->gmm_le)
			gps->gmm_le = alligator_ht_init(NULL);
		alligator_ht_insert(gps->gmm_le, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));

		double *bounds = calloc(le_size - 2, sizeof(double));
		for (uint64_t i = 2; i < le_size; i++)
		{
			json_t *le_obj = json_array_get(le, i);
			if (json_is_string(le_obj))
				bounds[i - 2] = strtod(json_string_value(le_obj), NULL);
			else if (json_is_number(le_obj))
				bounds[i - 2] = json_number_value(le_obj);
		}
		mm->le = bounds;
		mm->le_size = le_size - 2;

		if (!gps->mm)
			gps->mm = mm;
		else
			push_mapping_metric(gps->mm, mm);
	}

	json_t *_splited_tags = json_object_get(grok, "splited_tags");
	uint64_t splited_tags_outer = json_array_size(_splited_tags);
	for (uint64_t j = 0; j < splited_tags_outer; j++)
	{
		json_t *splited_tags = json_array_get(_splited_tags, j);
		if (!splited_tags)
			continue;
		uint64_t splited_tags_size = json_array_size(splited_tags);
		if (splited_tags_size <= 1)
			continue;
		if (!gps->gmm_splited_tags)
			gps->gmm_splited_tags = alligator_ht_init(NULL);
		grok_set_separator(gps, json_array_get(splited_tags, 0));
		for (uint64_t i = 1; i < splited_tags_size; i++)
		{
			json_t *splited_tags_obj = json_array_get(splited_tags, i);
			if (!json_is_string(splited_tags_obj))
				continue;
			grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
			gmm_node->key = strdup((char *)json_string_value(splited_tags_obj));
			gmm_node->metric_name = string_init_dupn((char *)json_string_value(splited_tags_obj), json_string_length(splited_tags_obj));
			alligator_ht_insert(gps->gmm_splited_tags, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));
		}
	}

	json_t *_splited_inherit_tag = json_object_get(grok, "splited_inherit_tag");
	uint64_t splited_inherit_outer = json_array_size(_splited_inherit_tag);
	for (uint64_t j = 0; j < splited_inherit_outer; j++)
	{
		json_t *splited_inherit_tag = json_array_get(_splited_inherit_tag, j);
		if (!splited_inherit_tag)
			continue;
		uint64_t splited_inherit_tag_size = json_array_size(splited_inherit_tag);
		if (!splited_inherit_tag_size)
			continue;
		if (!gps->gmm_splited_inherit_tag)
			gps->gmm_splited_inherit_tag = alligator_ht_init(NULL);
		/* Inherit tags are label names only — do not overwrite the split separator. */
		for (uint64_t i = 0; i < splited_inherit_tag_size; i++)
		{
			json_t *obj = json_array_get(splited_inherit_tag, i);
			if (!json_is_string(obj))
				continue;
			grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
			gmm_node->key = strdup((char *)json_string_value(obj));
			gmm_node->metric_name = string_init_dupn((char *)json_string_value(obj), json_string_length(obj));
			alligator_ht_insert(gps->gmm_splited_inherit_tag, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));
		}
	}

	json_t *splited_counters = json_object_get(grok, "splited_counter");
	uint64_t splited_counters_size = json_array_size(splited_counters);
	for (uint64_t j = 0; j < splited_counters_size; j++)
	{
		json_t *splited_counter = json_array_get(splited_counters, j);
		if (!splited_counter)
			continue;
		uint64_t splited_counter_size = json_array_size(splited_counter);
		if (splited_counter_size < 2)
			continue;
		json_t *jname = json_array_get(splited_counter, 0);
		json_t *jkey = json_array_get(splited_counter, 1);
		if (!json_is_string(jname) || !json_is_string(jkey))
			continue;
		grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
		gmm_node->metric_name = string_init_dupn((char *)json_string_value(jname), json_string_length(jname));
		gmm_node->key = strdup((char *)json_string_value(jkey));
		if (splited_counter_size > 2)
			grok_set_separator(gps, json_array_get(splited_counter, 2));
		if (!gps->gmm_splited_counter)
			gps->gmm_splited_counter = alligator_ht_init(NULL);
		alligator_ht_insert(gps->gmm_splited_counter, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));
	}

	json_t *splited_quantiles = json_object_get(grok, "splited_quantiles");
	uint64_t splited_quantiles_size = json_array_size(splited_quantiles);
	for (uint64_t j = 0; j < splited_quantiles_size; j++)
	{
		json_t *splited_quantile = json_array_get(splited_quantiles, j);
		mm = NULL;
		if (!splited_quantile)
			continue;
		uint64_t splited_percentiles_size = json_array_size(splited_quantile);
		if (splited_percentiles_size <= 2)
			continue;

		mm = calloc(1, sizeof(*mm));
		mm->percentile_buffer_min = -1;
		mm->percentile_calc_every = -1;
		mm->quantile_window = -1;
		mm->quantile_window_empty = -1;

		json_t *jname = json_array_get(splited_quantile, 0);
		json_t *jtemplate = json_array_get(splited_quantile, 1);
		if (!json_is_string(jname) || !json_is_string(jtemplate)) {
			free(mm);
			continue;
		}
		mm->metric_name = strdup((char *)json_string_value(jname));
		mm->template = strdup((char *)json_string_value(jname));
		if (splited_percentiles_size > 2)
			grok_set_separator(gps, json_array_get(splited_quantile, 2));
		grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
		gmm_node->key = strdup((char *)json_string_value(jtemplate));
		gmm_node->metric_name = string_init_dupn((char *)json_string_value(jname), json_string_length(jname));
		if (!gps->gmm_splited_quantile)
			gps->gmm_splited_quantile = alligator_ht_init(NULL);
		alligator_ht_insert(gps->gmm_splited_quantile, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));

		size_t nperc = splited_percentiles_size > 3 ? splited_percentiles_size - 3 : 0;
		int64_t *splited_percentiles = nperc ? calloc(nperc, sizeof(int64_t)) : NULL;
		for (uint64_t i = 3; i < splited_percentiles_size; i++)
			splited_percentiles[i - 3] = mapping_quantile_from_json(json_array_get(splited_quantile, i));

		mm->percentile = splited_percentiles;
		mm->percentile_size = (int64_t)nperc;

		if (!gps->mm)
			gps->mm = mm;
		else
			push_mapping_metric(gps->mm, mm);
	}

	json_t *splited_les = json_object_get(grok, "splited_bucket");
	uint64_t splited_les_outer = json_array_size(splited_les);
	for (uint64_t j = 0; j < splited_les_outer; j++)
	{
		json_t *splited_le = json_array_get(splited_les, j);
		mm = NULL;
		if (!splited_le)
			continue;
		uint64_t splited_le_size = json_array_size(splited_le);
		if (splited_le_size <= 2)
			continue;

		mm = calloc(1, sizeof(*mm));
		json_t *jname = json_array_get(splited_le, 0);
		json_t *jtemplate = json_array_get(splited_le, 1);
		if (!json_is_string(jname) || !json_is_string(jtemplate)) {
			free(mm);
			continue;
		}
		mm->metric_name = strdup((char *)json_string_value(jname));
		mm->template = strdup((char *)json_string_value(jname));
		if (splited_le_size > 2)
			grok_set_separator(gps, json_array_get(splited_le, 2));
		grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
		gmm_node->key = strdup((char *)json_string_value(jtemplate));
		gmm_node->metric_name = string_init_dupn((char *)json_string_value(jname), json_string_length(jname));
		if (!gps->gmm_splited_le)
			gps->gmm_splited_le = alligator_ht_init(NULL);
		alligator_ht_insert(gps->gmm_splited_le, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));

		size_t nb = splited_le_size > 3 ? splited_le_size - 3 : 0;
		double *bounds = nb ? calloc(nb, sizeof(double)) : NULL;
		for (uint64_t i = 3; i < splited_le_size; i++)
		{
			json_t *obj = json_array_get(splited_le, i);
			if (json_is_string(obj))
				bounds[i - 3] = strtod(json_string_value(obj), NULL);
			else if (json_is_number(obj))
				bounds[i - 3] = json_number_value(obj);
		}
		mm->le = bounds;
		mm->le_size = (int64_t)nb;

		if (!gps->mm)
			gps->mm = mm;
		else
			push_mapping_metric(gps->mm, mm);
	}
	/* Global monotonic so delete+recreate never reuses epoch 1 vs carg's cached 1. */
	static uint32_t grok_mm_seq;
	uint32_t e = __atomic_add_fetch(&grok_mm_seq, 1, __ATOMIC_RELAXED);
	gps->mm_epoch = e ? e : 1;
}

int grok_push(json_t *grok) {
	json_t *jname = json_object_get(grok, "name");
	if (!jname || !json_is_string(jname) || !json_string_value(jname)) {
		glog(L_INFO, "grok_push: not specified 'name' option\n");
		return 0;
	}
	char *name = (char *)json_string_value(jname);

	json_t *jkey = json_object_get(grok, "key");
	if (!jkey || !json_is_string(jkey) || !json_string_value(jkey)) {
		glog(L_INFO, "grok_push: not specified 'key' option in '%s'\n", name);
		return 0;
	}
	char *key = (char *)json_string_value(jkey);

	json_t *jvalue = json_object_get(grok, "value");
	char *value = (jvalue && json_is_string(jvalue)) ? (char *)json_string_value(jvalue) : NULL;

	json_t *jmatch = json_object_get(grok, "match");
	if (!jmatch || !json_is_string(jmatch) || !json_string_value(jmatch)) {
		glog(L_INFO, "grok_push: not specified 'match' option in '%s'\n", name);
		return 0;
	}
	char *match = (char *)json_string_value(jmatch);
	size_t match_len = json_string_length(jmatch);

	grok_node *gn = calloc(1, sizeof(*gn));
	if (!gn)
		return 0;
	gn->name = strdup(name);
	if (value)
		gn->value = strdup(value);
	gn->match = string_init_dupn(match, match_len);
	json_t *jtype = json_object_get(grok, "type");
	if (jtype && json_is_string(jtype)) {
		const char *t = json_string_value(jtype);
		if (t && !strcasecmp(t, "gauge"))
			gn->is_gauge = 1;
	}

	/* Contexts are keyed by `key`, not metric `name` — multiple rules share one key.
	 * Hold life lock for table lookup/create, then pin with refs before unlocking. */
	grok_life_lock();
	grok_ds *gps = alligator_ht_search(ac->grok, grokds_compare, key, tommy_strhash_u32(0, key));
	if (!gps || gps->zombie)
	{
		gps = calloc(1, sizeof(*gps));
		if (!gps) {
			grok_life_unlock();
			grok_node_del(gn);
			return 0;
		}
		gps->hash = calloc(1, sizeof(alligator_ht));
		alligator_ht_init(gps->hash);
		gps->key = strdup(key);
		gps->refs = 0;
		pthread_rwlock_init(&gps->lock, NULL);
		grok_parse_multimetrics(gps, grok);
		alligator_ht_insert(ac->grok, &(gps->node), gps, tommy_strhash_u32(0, gps->key));
	}
	__atomic_fetch_add(&gps->refs, 1, __ATOMIC_ACQ_REL);
	grok_life_unlock();

	/* Multimetric options (counter/quantile/bucket/split*) are taken from the
	 * first rule that creates the key. Later rules under the same key only add matches. */

	pthread_rwlock_wrlock(&gps->lock);
	if (gps->zombie) {
		pthread_rwlock_unlock(&gps->lock);
		grok_put(gps);
		/* Key was deleted under us — recreate via a fresh push attempt. */
		grok_node_del(gn);
		return grok_push(grok);
	}
	grok_node *existing = alligator_ht_search(gps->hash, grok_compare, gn->name, tommy_strhash_u32(0, gn->name));
	if (existing) {
		alligator_ht_remove_existing(gps->hash, &(existing->node));
		grok_node_del(existing);
	}

	glog(L_DEBUG, "create grok node '%s' under key '%s', match '%s'\n", gn->name, key, gn->match->s);
	alligator_ht_insert(gps->hash, &(gn->node), gn, tommy_strhash_u32(0, gn->name));
	__atomic_fetch_add(&gps->want_epoch, 1, __ATOMIC_RELEASE);
	pthread_rwlock_unlock(&gps->lock);
	grok_put(gps);

	return 1;
}
