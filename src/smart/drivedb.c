#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include "dstructures/ngram/ngram.h"
#include "smart/drivedb.h"

drive_settings builtin_knowndrives[] = {
#include "drivedb.h"
};

static inline int preset_identificators_cmp(const void* arg, const void* obj) {
	int *key = (int*)arg;
	const drivedb_struct* node = (const drivedb_struct*)obj;
	return *key != node->key;
}

alligator_ht *presets_load(const char *presets) {
	if (!presets)
		return NULL;
	if (!*presets)
		return NULL;

	alligator_ht *ret = alligator_ht_init(NULL);
	{
		char field[2048];
		size_t presets_len = strlen(presets);
		for (uint64_t j = 0; j < presets_len; ++j) {
			uint64_t field_len = str_get_next(presets, field, 2048, " ", &j);
			if (field_len == 2)
				continue;

			uint64_t k = 0;
			char attr[255];
			char raw[255];
			char attr_desc[1024];
			str_get_next(field, attr, 255, ",", &k); ++k;
			uint8_t iattr = atoi(attr);
			str_get_next(field, raw, 255, ",", &k); ++k;
			str_get_next(field, attr_desc, 1024, ",", &k);
			//printf("\tfields: '%"PRIu8"', '%s'\n", iattr, attr_desc);

			drivedb_struct *ddb = malloc(sizeof(*ddb));
			ddb->key = iattr;
			ddb->identificator = strdup(attr_desc);
			alligator_ht_insert(ret, &(ddb->node), ddb, tommy_inthash_u32(ddb->key));
		}
	}
	return ret;
}

drive_settings_extended *drivedb_search(ngram_index_t *ngram_table, char *query) {
	ngram_node_t **results;

	uint64_t res = ngram_token_search(ngram_table, &results, DRIVEDB_NGRAM_LEN, query);
	ngram_node_t *filtered_results = ngram_filter(results, res);
	free(results);

	for (uint64_t i = 0; i < filtered_results->count; ++i) {
		regex_t regex;
		drive_settings_extended *dse = filtered_results->data[i];
		int reg_ret = regcomp(&regex, dse->ds.modelregexp, REG_EXTENDED);
		if (reg_ret) {
			fprintf(stderr, "Could not compile regex: %s\n", dse->ds.modelregexp);
			continue;
		}

		reg_ret = regexec(&regex, query, 0, NULL, 0);
		regfree(&regex);
		if (reg_ret)
			continue;

		dse = filtered_results->data[i];
		if (!dse->presets_identificators) {
			dse->presets_identificators = presets_load(dse->ds.presets);
		}

		return dse;
		// presets is -v 170,raw48,Grown_Failing_Block_Ct -v 171,raw48,Program_Fail_Count -v 172,raw48,Erase_Fail_Count -v 173,raw48,Wear_Leveling_Count -v 174,raw48,Unexpect_Power_Loss_Ct -v 181,raw16,Non4k_Aligned_Access -v 183,raw48,SATA_Iface_Downshift -v 189,raw48,Factory_Bad_Block_Ct -v 202,raw48,Percent_Lifetime_Used
	}
	ngram_filter_free(filtered_results);

	//ngram_clear(ngram_table, NULL);
	return NULL;
}

ngram_index_t* drivedb_load() {
	ngram_index_t *ngram_table = ngram_index_init(250);

	uint64_t drives = sizeof(builtin_knowndrives) / sizeof(builtin_knowndrives[0]);
	uint64_t i = 0;
	for (i = 0; i < drives; ++i) {
		drive_settings_extended *dse = calloc(1, sizeof(*dse));
		memcpy(&dse->ds, &builtin_knowndrives[i], sizeof(builtin_knowndrives[i]));
		if (!dse->ds.modelregexp) {
			break;
		}

		ngram_token_add(ngram_table, DRIVEDB_NGRAM_LEN, dse->ds.modelregexp, dse);
	}

	return ngram_table;
}

char* drivedb_get_identificator(drive_settings_extended *dse, int id) {
	drivedb_struct *ddb = alligator_ht_search(dse->presets_identificators, preset_identificators_cmp, &id, tommy_inthash_u32(id));
	if (!ddb)
		return NULL;

	return  ddb->identificator;
}
