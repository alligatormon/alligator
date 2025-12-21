#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "common/aggregator.h"
#include "dstructures/tommy.h"
#include "metric/namespace.h"
#include "common/validator.h"
#include "common/logs.h"
#include "grok/type.h"
#include "mapping/type.h"
#include "common/http.h"
#include "common/selector.h"

#define MAX_PATTERNS 1024
#define MAX_LINE_LEN 1024
#define MAX_REGEX_LEN 2048

extern aconf *ac;

typedef struct grok_ctx_cb {
	string *line;
	OnigRegion *region;
	alligator_ht *lbl;
	alligator_ht *splited_lbl_inherited;
	alligator_ht **splited_lbl;
	uint64_t splited_lbl_size;
	double value;
	int value_set;
	char metric_name[255];
	double *quantile_values;
	double *le_values;
	double *counter_values;
	double *splited_quantile_values;
	double *splited_le_values;
	int64_t *splited_counter_values;
	grok_ds* gds;
	grok_node *gn;
	context_arg *carg;
	uint64_t quantile_index;
	uint64_t le_index;
	uint64_t counter_index;
	uint64_t splited_quantile_index;
	uint64_t splited_le_index;
	uint64_t splited_counter_index;
} grok_ctx_cb;

int load_patterns(const char *filename, grok_pattern_node *patterns, size_t *count) {
	FILE *f = fopen(filename, "r");
	if (!f) return -1;

	char line[65535];

	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#' || strlen(line) < 2) continue;
		char key[1024], val[65535];
		if (sscanf(line, "%1023s %65534[^\n]", key, val) == 2) {
			strcpy(patterns[*count].name, key);
			strcpy(patterns[*count].regex, val);
			(*count)++;
		}
	}
	fclose(f);
	return 0;
}

int expand_grok_pattern(char *src, string *dst_pass, grok_pattern_node *patterns, int count, int recursive_call) {
	char *p = src;
	//char *out = dst_pass->s;
	int expanded = 0;
	while (*p) {
		if (strncmp(p, "%{", 2) == 0) {
			p += 2;
			char key[64] = {0}, field[64] = {0};
			int i = 0;
			while (*p && *p != '}' && *p != ':') key[i++] = *p++;
			key[i] = '\0';

			if (*p == ':') {
				p++;
				i = 0;
				while (*p && *p != '}') field[i++] = *p++;
				field[i] = '\0';
			}

			if (*p == '}') p++;

			const char *regex = ".*?";
			for (int j = 0; j < count; j++) {
				if (strcmp(patterns[j].name, key) == 0) {
					regex = patterns[j].regex;
					break;
				}
			}

			if (strlen(field) > 0 && !recursive_call)
				string_sprintf(dst_pass, "(?<%s>%s)", field, regex);
				//out += sprintf(out, "(?<%s>%s)", field, regex);
			else
				string_sprintf(dst_pass, "(?:%s)", regex);
				//out += sprintf(out, "(?:%s)", regex);

			++expanded;
		} else {
			string_cat(dst_pass, p++, 1);
			//*out++ = *p++;
		}
	}
	//*out = '\0';
	//*size = out - dst_pass->s;
	return expanded;
}

void grok_expand(string *src, string **dst, grok_pattern *patterns) {
	if (!patterns) {
		glog(L_ERROR, "No loaded grok patterns\n");
		return;
	}

	int initialized = 0;
	string *src_pass = src;
	string *dst_pass = *dst;
	int expanded = 1;
	for (uint64_t i = 0; expanded != 0; ++i) {
		dst_pass = string_init(src_pass->l * 2);
		expanded = expand_grok_pattern(src_pass->s, dst_pass, patterns->nodes, patterns->count, i);
		if (initialized)
			string_free(src_pass);
		src_pass = dst_pass;
		initialized = 1;
	}
	*dst = src_pass;
}

int grok_multimetric_hash_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((grok_multimetric_node*)obj)->key;

	return strcmp(s1, s2);
}

static int print_named_group(const UChar *name, const UChar *name_end, int ngroup_num, int *group_nums, regex_t *reg, void *arg)
{
	struct grok_ctx_cb *ctx = arg;

	const char *line = ctx->line->s;
	OnigRegion *region = ctx->region;

	for (int i = 0; i < ngroup_num; i++) {
		int gnum = group_nums[i];
		if (region->beg[gnum] >= 0) {
			size_t key_len = region->end[gnum] - region->beg[gnum];
			char key[key_len+1];
			size_t name_len = name_end - name;
			char *mname = (char*)name;
			grok_multimetric_node *gmm_node = NULL;
			strlcpy(key, (char*)line + region->beg[gnum], key_len+1);

			uint32_t hash = tommy_strhash_u32(0, mname);

			// splited inherit tags
			carglog(ctx->carg, L_TRACE, "processing: %s\n", mname);
			if (ctx->gds->gmm_splited_inherit_tag && (gmm_node = alligator_ht_search(ctx->gds->gmm_splited_inherit_tag, grok_multimetric_hash_compare, mname, hash))) {
				carglog(ctx->carg, L_TRACE, "\t>splited_inherit_tag\n");
				if (!ctx->gds->splited_inherit_tag_names)
					ctx->gds->splited_inherit_tag_names = string_tokens_new();
				if (!ctx->splited_lbl_inherited) {
					ctx->splited_lbl_inherited = alligator_ht_init(NULL);
				}

				labels_hash_insert_nocache(ctx->splited_lbl_inherited, mname, key);
			}

			if (ctx->gn->value && !strcmp(mname, ctx->gn->value)) {
				carglog(ctx->carg, L_TRACE, "\t>simple\n");
				ctx->value_set = 1;
				ctx->value = strtod(key, NULL);
				continue;
			}
			else if (!strncmp(mname, "__name__", 5)) {
				carglog(ctx->carg, L_TRACE, "\t>metric with custom name\n");
				ctx->value_set = 1;
				ctx->value_set = 1;
				strlcpy(ctx->metric_name, key, 255);
				continue;
			}
			// splited tags
			else if (ctx->gds->gmm_splited_tags && (gmm_node = alligator_ht_search(ctx->gds->gmm_splited_tags, grok_multimetric_hash_compare, mname, hash))) {
				carglog(ctx->carg, L_TRACE, "\t>splited_tags\n");
				if (!ctx->gds->splited_tags_names)
					ctx->gds->splited_tags_names = string_tokens_new();
				int64_t index;
				if ((index = string_tokens_check_or_add(ctx->gds->splited_tags_names, gmm_node->metric_name->s, gmm_node->metric_name->l))) {
					string_tokens* st = string_tokens_char_split_any(key, key_len, ctx->gds->separator->s);
					if (st) {
						if (!ctx->splited_lbl) {
							ctx->splited_lbl = calloc(1, sizeof(alligator_ht*)*st->l);
							ctx->splited_lbl_size = st->l;
						}
						for (uint64_t k = 0; k < ctx->splited_lbl_size; ++k) {
							if (ctx->splited_lbl[k] == NULL)
								ctx->splited_lbl[k] = alligator_ht_init(NULL);
							labels_hash_insert_nocache(ctx->splited_lbl[k], mname, st->str[k]->s);
						}
						string_tokens_free(st);
					}
				}
				continue;
			}
			// splited quantile
			else if (ctx->gds->gmm_splited_quantile && (gmm_node = alligator_ht_search(ctx->gds->gmm_splited_quantile, grok_multimetric_hash_compare, mname, hash))) {
				carglog(ctx->carg, L_TRACE,"\t>splited_quantile\n");
				int64_t index;
				if (!ctx->gds->splited_quantile_names)
					ctx->gds->splited_quantile_names = string_tokens_new();
				if (!ctx->splited_quantile_values)
					ctx->splited_quantile_values = calloc(1, sizeof(double) * alligator_ht_count(ctx->gds->gmm_splited_quantile));
				if ((index = string_tokens_check_or_add(ctx->gds->splited_quantile_names, gmm_node->metric_name->s, gmm_node->metric_name->l))) {
					ctx->splited_quantile_values[index-1] = strtod(key, NULL);
					string_tokens* st = string_tokens_char_split_any(key, key_len, ctx->gds->separator->s);
					carglog(ctx->carg, L_TRACE,"\t\t>splited_quantile splited: %"PRIu64", value: %f\n", st ? st->l : 0, ctx->splited_quantile_values[index-1]);
					if (st) {
						if (!ctx->splited_lbl) {
							ctx->splited_lbl = calloc(1, sizeof(alligator_ht*)*st->l);
							ctx->splited_lbl_size = st->l;
						}
						for (uint64_t k = 0; k < ctx->splited_lbl_size; ++k) {
							if (ctx->splited_lbl[k] == NULL)
								ctx->splited_lbl[k] = alligator_ht_init(NULL);
							//labels_hash_insert_nocache(ctx->splited_lbl[k], mname, st->str[k]->s);
						}
						string_tokens_free(st);
					}
					//else {
					//	ctx->splited_quantile_values[ctx->splited_quantile_index] = strtod(key, NULL);
					//	carglog(ctx->carg, L_TRACE,"\t\t>splited_quantile simple: %f\n", ctx->splited_quantile_values[ctx->splited_quantile_index]);
					//	ctx->splited_lbl_size = 1;
					//}
					ctx->splited_quantile_index++;
				}
				continue;
			}
			// splited counter
			else if (ctx->gds->gmm_splited_counter && (gmm_node = alligator_ht_search(ctx->gds->gmm_splited_counter, grok_multimetric_hash_compare, mname, hash))) {
				carglog(ctx->carg, L_TRACE, "\t>splited_counter\n");
				int64_t index;
				if (!ctx->gds->splited_counter_names)
					ctx->gds->splited_counter_names = string_tokens_new();
				if (!ctx->splited_counter_values)
					ctx->splited_counter_values = calloc(1, sizeof(int64_t) * alligator_ht_count(ctx->gds->gmm_splited_counter));
				if ((index = string_tokens_check_or_add(ctx->gds->splited_counter_names, gmm_node->metric_name->s, gmm_node->metric_name->l)))
				{
					ctx->splited_counter_values[index-1] = strtoll(key, NULL, 10);
					string_tokens* st = string_tokens_char_split_any(key, key_len, ctx->gds->separator->s);
					carglog(ctx->carg, L_TRACE,"\t\t>splited_counter splited: %"PRIu64", value: %f\n", st ? st->l : 0, ctx->splited_counter_values[index-1]);
					if (st) {
						if (!ctx->splited_lbl) {
							ctx->splited_lbl = calloc(1, sizeof(alligator_ht*)*st->l);
							ctx->splited_lbl_size = st->l;
						}
						for (uint64_t k = 0; k < ctx->splited_lbl_size; ++k) {
							if (ctx->splited_lbl[k] == NULL)
								ctx->splited_lbl[k] = alligator_ht_init(NULL);
							labels_hash_insert_nocache(ctx->splited_lbl[k], mname, st->str[k]->s);
						}
					}
					//else {
					//	ctx->splited_counter_values[ctx->splited_counter_index] = strtod(key, NULL);
					//	carglog(ctx->carg, L_TRACE,"\t\t>splited_counter simple: %f\n", ctx->splited_counter_values[ctx->splited_counter_index]);
					//	ctx->splited_lbl_size = 1;
					//}
					ctx->splited_counter_index++;
				}
				continue;
			} // splited bucket
			else if (ctx->gds->gmm_splited_le && (gmm_node = alligator_ht_search(ctx->gds->gmm_splited_le, grok_multimetric_hash_compare, mname, hash))) {
				carglog(ctx->carg, L_TRACE, "\t>splited_le\n");
				int64_t index;
				if (!ctx->gds->splited_le_names)
					ctx->gds->splited_le_names = string_tokens_new();
				if (!ctx->splited_le_values)
					ctx->splited_le_values = calloc(1, sizeof(double) * alligator_ht_count(ctx->gds->gmm_splited_le));
				if ((index = string_tokens_check_or_add(ctx->gds->splited_le_names, gmm_node->metric_name->s, gmm_node->metric_name->l))) {
					ctx->splited_le_values[index-1] = strtod(key, NULL);
					string_tokens* st = string_tokens_char_split_any(key, key_len, ctx->gds->separator->s);
					carglog(ctx->carg, L_TRACE,"\t\t>splited_le splited: %"PRIu64", value: %f\n", st ? st->l : 0, ctx->splited_le_values[index-1]);
					if (st) {
						if (!ctx->splited_lbl) {
							ctx->splited_lbl = calloc(1, sizeof(alligator_ht*)*st->l);
							ctx->splited_lbl_size = st->l;
						}
						for (uint64_t k = 0; k < ctx->splited_lbl_size; ++k) {
							if (ctx->splited_lbl[k] == NULL)
								ctx->splited_lbl[k] = alligator_ht_init(NULL);
							//labels_hash_insert_nocache(ctx->splited_lbl[k], mname, st->str[k]->s);
						}
					}
					//else {
					//	ctx->splited_le_values[ctx->splited_le_index] = strtod(key, NULL);
					//	carglog(ctx->carg, L_TRACE,"\t\t>splited_le simple: %f\n", ctx->splited_le_values[ctx->splited_le_index]);
					//	ctx->splited_lbl_size = 1;
					//}
					ctx->splited_le_index++;
				}
				continue;
			}
			// quantile
			else if (ctx->gds->gmm_quantile && (gmm_node = alligator_ht_search(ctx->gds->gmm_quantile, grok_multimetric_hash_compare, mname, hash))) {
				carglog(ctx->carg, L_TRACE, "\t>quantile\n");
				int64_t index;
				if (!ctx->gds->quantile_names)
					ctx->gds->quantile_names = string_tokens_new();
				if (!ctx->quantile_values)
					ctx->quantile_values = calloc(1, sizeof(double) * alligator_ht_count(ctx->gds->gmm_quantile));
				if ((index = string_tokens_check_or_add(ctx->gds->quantile_names, gmm_node->metric_name->s, gmm_node->metric_name->l))) {
					ctx->quantile_values[index-1] = strtod(key, NULL);
					carglog(ctx->carg, L_TRACE,"\t\t>quantile first: %f\n", ctx->quantile_values[ctx->quantile_index]);
				}
				else {
					ctx->quantile_values[ctx->quantile_index] = strtod(key, NULL);
					carglog(ctx->carg, L_TRACE,"\t\t>quantile secondary: %f\n", ctx->quantile_values[ctx->quantile_index]);
				}
				ctx->quantile_index++;
				continue;
			}

			// simple bucket
			else if (ctx->gds->gmm_le && (gmm_node = alligator_ht_search(ctx->gds->gmm_le, grok_multimetric_hash_compare, mname, hash))) {
				carglog(ctx->carg, L_TRACE, "\t>le\n");
				int64_t index;
				if (!ctx->gds->le_names)
					ctx->gds->le_names = string_tokens_new();
				if (!ctx->le_values)
					ctx->le_values = calloc(1, sizeof(double) * alligator_ht_count(ctx->gds->gmm_le));
				if ((index = string_tokens_check_or_add(ctx->gds->le_names, gmm_node->metric_name->s, gmm_node->metric_name->l))) {
					ctx->le_values[index-1] = strtod(key, NULL);
					carglog(ctx->carg, L_TRACE,"\t\t>le first: %f\n", ctx->le_values[index-1]);
				}
				else {
					ctx->le_values[ctx->le_index] = strtod(key, NULL);
					carglog(ctx->carg, L_TRACE,"\t\t>le secondary: %f\n", ctx->le_values[ctx->le_index]);;
				}
				ctx->le_index++;
				continue;
			}
			// simple counter
			else if (ctx->gds->gmm_counter && (gmm_node = alligator_ht_search(ctx->gds->gmm_counter, grok_multimetric_hash_compare, mname, hash))) {
				carglog(ctx->carg, L_TRACE,"\t>counter\n");
				int64_t index;
				if (!ctx->gds->counter_names)
					ctx->gds->counter_names = string_tokens_new();
				if (!ctx->counter_values)
					ctx->counter_values = calloc(1, sizeof(double) * alligator_ht_count(ctx->gds->gmm_counter));
				if ((index = string_tokens_check_or_add(ctx->gds->counter_names, gmm_node->metric_name->s, gmm_node->metric_name->l)))
				{
					ctx->counter_values[index-1] = strtod(key, NULL);
					carglog(ctx->carg, L_TRACE,"\t\t>counter first: %f\n", ctx->counter_values[index-1]);
				} else {
					ctx->counter_values[ctx->counter_index] = strtod(key, NULL);
					carglog(ctx->carg, L_TRACE,"\t\t>counter secondary: %f\n", ctx->counter_values[ctx->counter_index]);
				}
				ctx->counter_index++;
				continue;
			}
			metric_label_value_validator_normalizer(key, key_len);
			metric_name_normalizer(mname, name_len);
			labels_hash_insert_nocache(ctx->lbl, mname, key);

			//printf("  %.*s = %.*s\n",
			//	   (int)(name_end - name), (const char *)name,
			//	   region->end[gnum] - region->beg[gnum],
			//	   line + region->beg[gnum]);
		}
	}
	return 0;
}

void grok_expand_free(char **src, size_t count) {
	for (int i = 0; i < count; ++i) {
		free(src[i]);
	}
	free(src);
}

void grok_handler_callback(void *funcarg, void* arg)
{
	grok_ctx_cb *ctx = (grok_ctx_cb*)funcarg;
	grok_node *gn = arg;

	//regex_t *reg;
	OnigErrorInfo einfo;
 
	if (!gn->reg) {
		int r = onig_new(&gn->reg, (UChar *)gn->expanded_match->s, (UChar *)(gn->expanded_match->s + gn->expanded_match->l), ONIG_OPTION_NONE, ONIG_ENCODING_UTF8, ONIG_SYNTAX_DEFAULT, &einfo);
		if (r != ONIG_NORMAL) {
			carglog(ctx->carg, L_ERROR, "Onig is not normal regex: '%s'\n", gn->expanded_match->s);
			return;
		}
	}

	ctx->region = onig_region_new();
	int r = onig_search(gn->reg, (UChar *)ctx->line->s, (UChar *)(ctx->line->s + ctx->line->l), (UChar *)ctx->line->s, (UChar *)(ctx->line->s + ctx->line->l), ctx->region, ONIG_OPTION_NONE);
	carglog(ctx->carg, L_TRACE, "matching line (%zu) (r: %d) '%s' with pattern '%s'\n", ctx->line->l, r, ctx->line->s, gn->expanded_match->s);

	ctx->gn = gn;

	if (r >= 0) {
		carglog(ctx->carg, L_DEBUG, "++++ grok_handler_callback loop ++++\n");
		ctx->lbl = alligator_ht_init(NULL);
		ctx->quantile_index = 0;
		ctx->counter_index = 0;
		ctx->le_index = 0;
		ctx->splited_quantile_index = 0;
		ctx->splited_counter_index = 0;
		ctx->splited_le_index = 0;
		onig_foreach_name(gn->reg, print_named_group, ctx);
		for (uint64_t i = 0; i < alligator_ht_count(ctx->gds->gmm_le); ++i) {
			alligator_ht *hash = labels_dup(ctx->lbl);
			carglog(ctx->carg, L_TRACE, "le adding metric: '%s' = %f\n", ctx->gds->le_names->str[i]->s, ctx->le_values[i]);
			metric_update(ctx->gds->le_names->str[i]->s, hash, &ctx->le_values[i], DATATYPE_DOUBLE, ctx->carg);
		}
		for (uint64_t i = 0; i < alligator_ht_count(ctx->gds->gmm_counter); ++i) {
			alligator_ht *hash = labels_dup(ctx->lbl);
			carglog(ctx->carg, L_TRACE, "counter adding metric: '%s' = %f\n", ctx->gds->counter_names->str[i]->s, ctx->counter_values[i]);
			metric_update(ctx->gds->counter_names->str[i]->s, hash, &ctx->counter_values[i], DATATYPE_DOUBLE, ctx->carg);
		}
		for (uint64_t i = 0; i < alligator_ht_count(ctx->gds->gmm_quantile); ++i) {
			alligator_ht *hash = labels_dup(ctx->lbl);
			carglog(ctx->carg, L_TRACE, "quantile adding metric: '%s' = %f\n", ctx->gds->quantile_names->str[i]->s, ctx->quantile_values[i]);
			metric_update(ctx->gds->quantile_names->str[i]->s, hash, &ctx->quantile_values[i], DATATYPE_DOUBLE, ctx->carg);
		}
		for (uint64_t i = 0; i < alligator_ht_count(ctx->gds->gmm_splited_counter); ++i) {
			carglog(ctx->carg, L_TRACE, "splited_counter adding metric: '%s': amount of metrics: %zu\n", ctx->gds->splited_counter_names->str[i]->s, ctx->splited_lbl_size);
			for (uint64_t k = 0; k < ctx->splited_lbl_size; ++k) {
				alligator_ht *hash = labels_dup(ctx->splited_lbl[k]);
				labels_merge(hash, ctx->splited_lbl_inherited);
				metric_update(ctx->gds->splited_counter_names->str[i]->s, hash, &ctx->splited_counter_values[i], DATATYPE_INT, ctx->carg);
			}

			if(ctx->gds->splited_counter_names && ctx->gds->splited_counter_names->l > i) {
				char count_name[1024];
				snprintf(count_name, 1023, "%s_count", ctx->gds->splited_counter_names->str[i]->s);
				alligator_ht *hash = labels_dup(ctx->lbl);
				carglog(ctx->carg, L_TRACE, "\tsplited_counter adding counter name[%"PRIu64"]: '%s' = %f\n", i, count_name, ctx->splited_lbl_size);
				metric_update(count_name, hash, &ctx->splited_lbl_size, DATATYPE_UINT, ctx->carg);
			}
			else {
				carglog(ctx->carg, L_ERROR, "Error: not found splited_counter variable (expected amount of splited counters: %lu, real: %lu), maybe it had been used already?\n", i, ctx->gds->splited_counter_names ? ctx->gds->splited_counter_names->l : 0);
			}
		}
		for (uint64_t i = 0; i < alligator_ht_count(ctx->gds->gmm_splited_le) && ctx->gds->splited_le_names; ++i) {
			carglog(ctx->carg, L_TRACE, "splited_le adding metric: '%s'\n", ctx->gds->splited_le_names->str[i]->s);
			alligator_ht *hash = labels_dup(ctx->lbl);
			metric_update(ctx->gds->splited_le_names->str[i]->s, hash, &ctx->splited_le_values[i], DATATYPE_DOUBLE, ctx->carg);
		}
		for (uint64_t i = 0; i < alligator_ht_count(ctx->gds->gmm_splited_quantile) && ctx->gds->splited_quantile_names; ++i) {
			carglog(ctx->carg, L_TRACE, "splited_quantile adding metric: '%s': amount of metrics: %zu\n", ctx->gds->splited_quantile_names->str[i]->s, ctx->splited_lbl_size);
			//alligator_ht *hash = labels_dup(ctx->lbl);
			for (uint64_t k = 0; k < ctx->splited_lbl_size; ++k) {
				alligator_ht *hash = labels_dup(ctx->splited_lbl[k]);
				labels_merge(hash, ctx->splited_lbl_inherited);
				carglog(ctx->carg, L_TRACE, "\tsplited_quantile adding metric name[%"PRIu64"]: '%s' = %f\n", i, ctx->gds->splited_quantile_names->str[i]->s, ctx->splited_quantile_values[i]);
				metric_add(ctx->gds->splited_quantile_names->str[i]->s, hash, &ctx->splited_quantile_values[i], DATATYPE_DOUBLE, ctx->carg);
			}
		}

		if (!ctx->value_set)
			ctx->value = 1;
		carglog(ctx->carg, L_TRACE, "add metric %s = %f\n", gn->name, ctx->value);
		metric_update(gn->name, ctx->lbl, &ctx->value, DATATYPE_DOUBLE, ctx->carg);
	} else if (r == ONIG_MISMATCH) {
	} else {
		char s[ONIG_MAX_ERROR_MESSAGE_LEN];
		onig_error_code_to_str((UChar* )s, r);
		carglog(ctx->carg, L_TRACE, "matching line ERROR: %s\n", s);
		return;
	}

	if (ctx->splited_lbl_size) {
		for (uint64_t k = 0; k < ctx->splited_lbl_size; ++k) {
			labels_hash_free(ctx->splited_lbl[k]);
		}
		ctx->splited_lbl_size = 0;
		free(ctx->splited_lbl);
	}

	if (ctx->le_values) {
		free(ctx->le_values);
		ctx->le_values = NULL;
	}
	if (ctx->gds->le_names) {
		string_tokens_free(ctx->gds->le_names);
		ctx->gds->le_names = NULL;
	}

	if (ctx->splited_le_values) {
		free(ctx->splited_le_values);
		ctx->splited_le_values = NULL;
	}
	if (ctx->gds->splited_le_names) {
		string_tokens_free(ctx->gds->splited_le_names);
		ctx->gds->splited_le_names = NULL;
	}

	if (ctx->quantile_values) {
		free(ctx->quantile_values);
		ctx->quantile_values = NULL;
	}
	if (ctx->gds->quantile_names) {
		string_tokens_free(ctx->gds->quantile_names);
		ctx->gds->quantile_names = NULL;
	}

	if (ctx->splited_quantile_values) {
		free(ctx->splited_quantile_values);
		ctx->splited_quantile_values = NULL;
	}
	if (ctx->gds->splited_quantile_names) {
		string_tokens_free(ctx->gds->splited_quantile_names);
		ctx->gds->splited_quantile_names = NULL;
	}

	if (ctx->counter_values) {
		free(ctx->counter_values);
		ctx->counter_values = NULL;
	}
	if (ctx->gds->counter_names) {
		string_tokens_free(ctx->gds->counter_names);
		ctx->gds->counter_names = NULL;
	}

	if (ctx->splited_counter_values) {
		free(ctx->splited_counter_values);
		ctx->splited_counter_values = NULL;
	}
	if (ctx->gds->splited_counter_names) {
		string_tokens_free(ctx->gds->splited_counter_names);
		ctx->gds->splited_counter_names = NULL;
	}
	if (ctx->splited_lbl_inherited) {
		labels_hash_free(ctx->splited_lbl_inherited);
		ctx->splited_lbl_inherited = NULL;
	}

	onig_region_free(ctx->region, 1);
}

void grok_handler_initial_patterns(void *funcarg, void* arg)
{
	grok_node *gn = arg;
	grok_expand(gn->match, &gn->expanded_match, ac->grok_patterns);
}

void grok_handler(char *metrics, size_t size, context_arg *carg)
{
	grok_ds* gds = grok_get(carg->name);
	if (!gds) {
		carglog(carg, L_ERROR, "not found grok keys with key '%s' for '%s'\n", carg->name, carg->key);
		return;
	}
	string *line = string_new();

	if (!gds->pattern_applied) {
		alligator_ht_foreach_arg(gds->hash, grok_handler_initial_patterns, NULL);
		carg->mm = mapping_copy(gds->mm);
		gds->pattern_applied = 1;
	}

	grok_ctx_cb *ctx = calloc(1, sizeof(*ctx));
	for (uint64_t i = 0; i < size; ) {
		size_t len = strcspn(metrics+i, "\n");
		memset(ctx, 0, sizeof(*ctx));
		ctx->gds = gds;
		ctx->carg = carg;
		ctx->line = line;
		string_cat(ctx->line, metrics+i, len);
		alligator_ht_foreach_arg(gds->hash, grok_handler_callback, ctx);

		string_null(ctx->line);

		i += len;
		i += strspn(metrics+i, "\n");
	}

	free(ctx);
	string_free(line);
	carg->parser_status = 1;
}

int grok_patterns_init() {
	size_t pat_count = 0;
	for (uint64_t i = 0; i < ac->grok_patterns_path->l; ++i) {
		pat_count += count_file_lines(ac->grok_patterns_path->str[i]->s);
	}

	grok_pattern *gp = calloc(1, sizeof(*gp));
	grok_pattern_node *patterns = calloc(1, sizeof(*patterns) * pat_count);
	size_t loaded = 0;
	for (uint64_t i = 0; i < ac->grok_patterns_path->l; ++i) {
		if (load_patterns(ac->grok_patterns_path->str[i]->s, patterns, &loaded)) {
			glog(L_ERROR, "Failed to load patterns.conf\n");
			return 1;
		}
	}
	gp->nodes = patterns;
	gp->count = loaded;

	ac->grok_patterns = gp;

	return 0;
}

string* grok_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		return string_init_add_auto(gen_http_query(0, hi->query, "", hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
	else if (hi->query)
		return string_init_alloc(hi->query, 0);
	else
		return NULL;
}

void grok_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("grok");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = grok_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = grok_mesg;
	strlcpy(actx->handler[0].key,"grok", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
