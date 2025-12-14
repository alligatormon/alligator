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

		json_t *buckets = json_object_get(grok, "buckets");
		uint64_t buckets_size = json_array_size(buckets);
		for (uint64_t j = 0; j < buckets_size; j++)
		{
			json_t *bucket = json_array_get(buckets, j);
			if (bucket) {
				uint64_t buckets_size = json_array_size(bucket);
				if (buckets_size > 2) {
					mm = calloc(1, sizeof(*mm));

					json_t *jname = json_array_get(bucket, 0);
					mm->metric_name = strdup((char*)json_string_value(jname));
					json_t *jtemplate = json_array_get(bucket, 1);
					mm->template = strdup((char*)json_string_value(jtemplate));
					grok_multimetric_node *gmm_node = calloc(1, sizeof(*gmm_node));
					gmm_node->key = strdup((char*)json_string_value(jtemplate));
					gmm_node->metric_name = string_init_dupn((char*)json_string_value(jname), json_string_length(jname));
					if (!gps->gmm_bucket)
						gps->gmm_bucket = alligator_ht_init(NULL);
					alligator_ht_insert(gps->gmm_bucket, &(gmm_node->node), gmm_node, tommy_strhash_u32(0, gmm_node->key));

					double *buckets = calloc(buckets_size - 2, sizeof(int64_t));
					for (uint64_t i = 2; i < buckets_size; i++)
					{
						json_t *bucket_obj = json_array_get(bucket, i);
						char *bucket_str = (char*)json_string_value(bucket_obj);
						double bucket_int = strtod(bucket_str, NULL);
						buckets[i-2] = bucket_int;
					}

					mm->bucket = buckets;
					mm->bucket_size = buckets_size - 2;
				}
			}

			if (mm) {
				if (!gps->mm)
					gps->mm = mm;
				else
					push_mapping_metric(gps->mm, mm);
			}
		}

		json_t *les = json_object_get(grok, "le");
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



		alligator_ht_insert(ac->grok, &(gps->node), gps, tommy_strhash_u32(0, gps->key));
	}
	glog(L_INFO, "create grok node %p: '%s', match '%s'\n", gn, gn->name, gn->match->s);
	alligator_ht_insert(gps->hash, &(gn->node), gn, tommy_strhash_u32(0, gn->name));

	return 1;
}
