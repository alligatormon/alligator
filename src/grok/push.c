#include "grok/type.h"
#include "common/logs.h"
#include "mapping/type.h"
#include "main.h"
extern aconf *ac;

int grok_push(json_t *grok) {
	json_t *jname = json_object_get(grok, "name");
	if (!jname) {
		glog(L_INFO, "grok_push: not specified 'name' option\n");
		return 0;
	}
	char *name = (char*)json_string_value(jname);

	json_t *jkey = json_object_get(grok, "key");
	if (!jkey) {
		glog(L_INFO, "grok_push: not specified 'key' option in '%s'\n", name);
		return 0;
	}
	char *key = (char*)json_string_value(jkey);

	json_t *jvalue = json_object_get(grok, "value");
	char *value = (char*)json_string_value(jvalue);

	json_t *jmatch = json_object_get(grok, "match");
	if (!jmatch) {
		glog(L_INFO, "grok_push: not specified 'match' option in '%s'\n", name);
		return 0;
	}
	char *match = (char*)json_string_value(jmatch);
	size_t match_len = json_string_length(jmatch);

	grok_node *gn = calloc(1, sizeof(*gn));

	gn->name = strdup(name);
	if (value)
		gn->value = strdup(value);
	gn->match = string_init_dupn(match, match_len);

	grok_ds *gps = alligator_ht_search(ac->grok, grokds_compare, gn->name, tommy_strhash_u32(0, gn->name));
	if (!gps)
	{
		gps = calloc(1, sizeof(*gps));
		gps->hash = calloc(1, sizeof(alligator_ht));
		alligator_ht_init(gps->hash);
		gps->key = strdup(key);


		json_t *counters = json_object_get(grok, "counter");
		uint64_t counters_size = json_array_size(counters);
		for (uint64_t j = 0; j < counters_size; j++)
		{
			json_t *counter = json_array_get(counters, j);
			if (counter) {
				uint64_t counter_size = json_array_size(counter);
				if (counter_size > 1) {
					json_t *jname = json_array_get(counter, 0);
					grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
					gmm_node->metric_name = string_init_dupn((char*)json_string_value(jname), json_string_length(jname));
					json_t *jkey = json_array_get(counter, 1);
					gmm_node->key = strdup((char*)json_string_value(jkey));
					if (!gps->gmm_counter)
						gps->gmm_counter = alligator_ht_init(NULL);
					alligator_ht_insert(gps->gmm_counter, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));
				}
			}
		}

		mapping_metric *mm = NULL;

		json_t *quantiles = json_object_get(grok, "quantiles");
		uint64_t quantiles_size = json_array_size(quantiles);
		for (uint64_t j = 0; j < quantiles_size; j++)
		{
			json_t *quantile = json_array_get(quantiles, j);
			if (quantile) {
				uint64_t percentiles_size = json_array_size(quantile);
				if (percentiles_size > 2) {
					mm = calloc(1, sizeof(*mm));

					json_t *jname = json_array_get(quantile, 0);
					mm->metric_name = strdup((char*)json_string_value(jname));
					json_t *jtemplate = json_array_get(quantile, 1);
					mm->template = strdup((char*)json_string_value(jtemplate));
					grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
					gmm_node->key = strdup((char*)json_string_value(jtemplate));
					gmm_node->metric_name = string_init_dupn((char*)json_string_value(jname), json_string_length(jname));
					if (!gps->gmm_quantile)
						gps->gmm_quantile = alligator_ht_init(NULL);
					alligator_ht_insert(gps->gmm_quantile, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));

					int64_t *percentiles = calloc(percentiles_size - 2, sizeof(int64_t));
					for (uint64_t i = 2; i < percentiles_size; i++)
					{
						json_t *percentile_obj = json_array_get(quantile, i);
						char *percentile_str = (char*)json_string_value(percentile_obj);
						int64_t percentile_int;
						if (percentile_str[0] > '0')
							percentile_int = -1;
						else
							percentile_int = strtoll(percentile_str+2, NULL, 10);
						percentiles[i-2] = percentile_int;
					}

					mm->percentile = percentiles;
					mm->percentile_size = percentiles_size - 2;
				}
			}

			if (mm) {
				if (!gps->mm)
					gps->mm = mm;
				else
					push_mapping_metric(gps->mm, mm);
			}
		}

		json_t *les = json_object_get(grok, "bucket");
		uint64_t les_size = json_array_size(les);
		for (uint64_t j = 0; j < les_size; j++)
		{
			json_t *le = json_array_get(les, j);
			if (le) {
				uint64_t les_size = json_array_size(le);
				if (les_size > 2) {
					mm = calloc(1, sizeof(*mm));

					json_t *jname = json_array_get(le, 0);
					mm->metric_name = strdup((char*)json_string_value(jname));
					json_t *jtemplate = json_array_get(le, 1);
					mm->template = strdup((char*)json_string_value(jtemplate));
					grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
					gmm_node->key = strdup((char*)json_string_value(jtemplate));
					gmm_node->metric_name = string_init_dupn((char*)json_string_value(jname), json_string_length(jname));
					if (!gps->gmm_le)
						gps->gmm_le = alligator_ht_init(NULL);
					alligator_ht_insert(gps->gmm_le, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));

					double *les = calloc(les_size - 2, sizeof(int64_t));
					for (uint64_t i = 2; i < les_size; i++)
					{
						json_t *le_obj = json_array_get(le, i);
						char *le_str = (char*)json_string_value(le_obj);
						double le_int = strtod(le_str, NULL);
						les[i-2] = le_int;
					}

					mm->le = les;
					mm->le_size = les_size - 2;
				}
			}

			if (mm) {
				if (!gps->mm)
					gps->mm = mm;
				else
					push_mapping_metric(gps->mm, mm);
			}
		}


		json_t *_splited_tags = json_object_get(grok, "splited_tags");
		uint64_t splited_tags_size = json_array_size(_splited_tags);
		for (uint64_t j = 0; j < splited_tags_size; j++)
		{
			json_t *splited_tags = json_array_get(_splited_tags, j);
			if (splited_tags) {
				uint64_t splited_tags_size = json_array_size(splited_tags);
				if (splited_tags_size > 1) {
					if (!gps->gmm_splited_tags)
						gps->gmm_splited_tags = alligator_ht_init(NULL);
					for (uint64_t i = 1; i < splited_tags_size; i++)
					{
						grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
						json_t *jseparator = json_array_get(splited_tags, 0);
						gps->separator = string_init_dupn((char*)json_string_value(jseparator), json_string_length(jseparator));

						json_t *splited_tags_obj = json_array_get(splited_tags, i);
						char *splited_tags_str = (char*)json_string_value(splited_tags_obj);
						gmm_node->key = strdup(splited_tags_str);
						gmm_node->metric_name = string_init_dupn((char*)json_string_value(splited_tags_obj), json_string_length(splited_tags_obj));
						alligator_ht_insert(gps->gmm_splited_tags, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));
					}
				}
			}
		}


		json_t *_splited_inherit_tag = json_object_get(grok, "splited_inherit_tag");
		uint64_t splited_inherit_tag_size = json_array_size(_splited_inherit_tag);
		for (uint64_t j = 0; j < splited_inherit_tag_size; j++)
		{
			json_t *splited_inherit_tag = json_array_get(_splited_inherit_tag, j);
			if (splited_inherit_tag) {
				uint64_t splited_inherit_tag_size = json_array_size(splited_inherit_tag);
				if (splited_inherit_tag_size > 0) {
					if (!gps->gmm_splited_inherit_tag)
						gps->gmm_splited_inherit_tag = alligator_ht_init(NULL);
					for (uint64_t i = 0; i < splited_inherit_tag_size; i++)
					{
						grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
						json_t *jseparator = json_array_get(splited_inherit_tag, 0);
						gps->separator = string_init_dupn((char*)json_string_value(jseparator), json_string_length(jseparator));

						json_t *splited_inherit_tag_obj = json_array_get(splited_inherit_tag, i);
						char *splited_inherit_tag_str = (char*)json_string_value(splited_inherit_tag_obj);
						gmm_node->key = strdup(splited_inherit_tag_str);
						gmm_node->metric_name = string_init_dupn((char*)json_string_value(splited_inherit_tag_obj), json_string_length(splited_inherit_tag_obj));
						alligator_ht_insert(gps->gmm_splited_inherit_tag, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));
					}
				}
			}
		}


		json_t *splited_counters = json_object_get(grok, "splited_counter");
		uint64_t splited_counters_size = json_array_size(splited_counters);
		for (uint64_t j = 0; j < splited_counters_size; j++)
		{
			json_t *splited_counter = json_array_get(splited_counters, j);
			if (splited_counter) {
				uint64_t splited_counter_size = json_array_size(splited_counter);
				if (splited_counter_size > 1) {
					json_t *jname = json_array_get(splited_counter, 0);
					grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
					gmm_node->metric_name = string_init_dupn((char*)json_string_value(jname), json_string_length(jname));
					json_t *jkey = json_array_get(splited_counter, 1);
					gmm_node->key = strdup((char*)json_string_value(jkey));
					json_t *jseparator = json_array_get(splited_counter, 2);
					gps->separator = string_init_dupn((char*)json_string_value(jseparator), json_string_length(jseparator));
					if (!gps->gmm_splited_counter)
						gps->gmm_splited_counter = alligator_ht_init(NULL);
					alligator_ht_insert(gps->gmm_splited_counter, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));
				}
			}
		}

		json_t *splited_quantiles = json_object_get(grok, "splited_quantiles");
		uint64_t splited_quantiles_size = json_array_size(splited_quantiles);
		for (uint64_t j = 0; j < splited_quantiles_size; j++)
		{
			json_t *splited_quantile = json_array_get(splited_quantiles, j);
			if (splited_quantile) {
				uint64_t splited_percentiles_size = json_array_size(splited_quantile);
				if (splited_percentiles_size > 2) {
					mm = calloc(1, sizeof(*mm));

					json_t *jname = json_array_get(splited_quantile, 0);
					mm->metric_name = strdup((char*)json_string_value(jname));
					json_t *jtemplate = json_array_get(splited_quantile, 1);
					mm->template = strdup((char*)json_string_value(jtemplate));
					json_t *jseparator = json_array_get(splited_quantile, 2);
					gps->separator = string_init_dupn((char*)json_string_value(jseparator), json_string_length(jseparator));
					grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
					gmm_node->key = strdup((char*)json_string_value(jtemplate));
					gmm_node->metric_name = string_init_dupn((char*)json_string_value(jname), json_string_length(jname));
					if (!gps->gmm_splited_quantile)
						gps->gmm_splited_quantile = alligator_ht_init(NULL);
					alligator_ht_insert(gps->gmm_splited_quantile, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));

					int64_t *splited_percentiles = calloc(splited_percentiles_size - 3, sizeof(int64_t));
					for (uint64_t i = 3; i < splited_percentiles_size; i++)
					{
						json_t *splited_percentile_obj = json_array_get(splited_quantile, i);
						char *splited_percentile_str = (char*)json_string_value(splited_percentile_obj);
						int64_t splited_percentile_int;
						if (splited_percentile_str[0] > '0')
							splited_percentile_int = -1;
						else
							splited_percentile_int = strtoll(splited_percentile_str+2, NULL, 10);
						splited_percentiles[i-3] = splited_percentile_int;
					}

					mm->percentile = splited_percentiles;
					mm->percentile_size = splited_percentiles_size - 3;
				}
			}

			if (mm) {
				if (!gps->mm)
					gps->mm = mm;
				else
					push_mapping_metric(gps->mm, mm);
			}
		}

		json_t *splited_les = json_object_get(grok, "splited_bucket");
		uint64_t splited_les_size = json_array_size(splited_les);
		for (uint64_t j = 0; j < splited_les_size; j++)
		{
			json_t *splited_le = json_array_get(splited_les, j);
			if (splited_le) {
				uint64_t splited_les_size = json_array_size(splited_le);
				if (splited_les_size > 2) {
					mm = calloc(1, sizeof(*mm));

					json_t *jname = json_array_get(splited_le, 0);
					mm->metric_name = strdup((char*)json_string_value(jname));
					json_t *jtemplate = json_array_get(splited_le, 1);
					mm->template = strdup((char*)json_string_value(jtemplate));
					json_t *jseparator = json_array_get(splited_le, 2);
					gps->separator = string_init_dupn((char*)json_string_value(jseparator), json_string_length(jseparator));
					grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
					gmm_node->key = strdup((char*)json_string_value(jtemplate));
					gmm_node->metric_name = string_init_dupn((char*)json_string_value(jname), json_string_length(jname));

					if (!gps->gmm_splited_le)
						gps->gmm_splited_le = alligator_ht_init(NULL);
					alligator_ht_insert(gps->gmm_splited_le, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));

					double *splited_les = calloc(splited_les_size - 3, sizeof(int64_t));
					for (uint64_t i = 3; i < splited_les_size; i++)
					{
						json_t *splited_le_obj = json_array_get(splited_le, i);
						char *splited_le_str = (char*)json_string_value(splited_le_obj);
						double splited_le_int = strtod(splited_le_str, NULL);
						splited_les[i-3] = splited_le_int;
					}

					mm->le = splited_les;
					mm->le_size = splited_les_size - 3;
				}
			}

			if (mm) {
				if (!gps->mm)
					gps->mm = mm;
				else
					push_mapping_metric(gps->mm, mm);
			}
		}



		alligator_ht_insert(ac->grok, &(gps->node), gps, tommy_strhash_u32(0, gps->key));
	}
	glog(L_INFO, "create grok node %p: '%s', match '%s'\n", gn, gn->name, gn->match->s);
	alligator_ht_insert(gps->hash, &(gn->node), gn, tommy_strhash_u32(0, gn->name));

	return 1;
}
