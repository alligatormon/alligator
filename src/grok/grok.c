#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <pcre.h>
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

#define GROK_EXPAND_MAX_PASSES 256
#define GROK_PCRE_MATCH_LIMIT 100000

extern aconf *ac;

#ifndef PCRE_UTF8
#define PCRE_UTF8 0
#endif
#ifndef PCRE_UCP
#define PCRE_UCP 0
#endif
#ifndef PCRE_DUPNAMES
#define PCRE_DUPNAMES 0
#endif
#ifndef PCRE_NO_UTF8_CHECK
#define PCRE_NO_UTF8_CHECK 0
#endif

#define GROK_PCRE_OPTIONS (PCRE_UTF8 | PCRE_UCP | PCRE_DUPNAMES)

void grok_sanitize_capture_name(const char *in, char *out, size_t outsz)
{
	if (!out || outsz < 2)
		return;
	out[0] = '\0';
	if (!in || !*in) {
		out[0] = '_';
		out[1] = '\0';
		return;
	}

	size_t in_len = strlen(in);
	const char *end = in + in_len;
	const char *colon = strrchr(in, ':');
	if (colon && colon != in) {
		const char *type = colon + 1;
		if (!strcasecmp(type, "int") || !strcasecmp(type, "float") || !strcasecmp(type, "string"))
			end = colon;
	}

	size_t o = 0;
	int last_us = 0;
	for (const char *p = in; p < end && o + 1 < outsz; ++p) {
		unsigned char c = (unsigned char)*p;
		if (c == '[')
			continue;
		if (c == ']') {
			if (p + 1 < end && o && !last_us) {
				out[o++] = '_';
				last_us = 1;
			}
			continue;
		}
		if (c == '-' || c == '.' || c == '/' || c == ' ' || c == ':') {
			if (o && !last_us) {
				out[o++] = '_';
				last_us = 1;
			}
			continue;
		}
		if (isalnum(c) || c == '_') {
			if (o == 0 && isdigit(c)) {
				out[o++] = '_';
				if (o + 1 >= outsz)
					break;
			}
			out[o++] = (char)c;
			last_us = 0;
			continue;
		}
		if (o && !last_us) {
			out[o++] = '_';
			last_us = 1;
		}
	}
	while (o && out[o - 1] == '_')
		--o;
	if (!o) {
		out[0] = '_';
		out[1] = '\0';
		return;
	}
	out[o] = '\0';
}

typedef struct grok_ctx_cb {
	string *line;
	int *ovector;
	alligator_ht *lbl;
	alligator_ht *splited_lbl_inherited;
	alligator_ht **splited_lbl;
	uint64_t splited_lbl_size;
	double value;
	int value_set;
	int custom_name;
	char metric_name[255];
	double *quantile_values;
	double *le_values;
	double *counter_values;
	double *gauge_values;
	/* Per-metric, per-split values for array fields. */
	double **splited_quantile_values;
	uint64_t *splited_quantile_counts;
	double **splited_le_values;
	uint64_t *splited_le_counts;
	int64_t **splited_counter_values;
	uint64_t *splited_counter_counts;
	string_tokens *quantile_names;
	string_tokens *le_names;
	string_tokens *counter_names;
	string_tokens *gauge_names;
	string_tokens *splited_quantile_names;
	string_tokens *splited_le_names;
	string_tokens *splited_counter_names;
	string_tokens *splited_tags_names;
	grok_ds* gds;
	grok_node *gn;
	context_arg *carg;
} grok_ctx_cb;

static int pattern_upsert(grok_pattern_node *patterns, size_t *count, size_t max_count, const char *key, const char *val)
{
	for (size_t i = 0; i < *count; i++) {
		if (!strcmp(patterns[i].name, key)) {
			strlcpy(patterns[i].regex, val, sizeof(patterns[i].regex));
			return 0;
		}
	}
	if (*count >= max_count)
		return -1;
	strlcpy(patterns[*count].name, key, sizeof(patterns[*count].name));
	strlcpy(patterns[*count].regex, val, sizeof(patterns[*count].regex));
	(*count)++;
	return 0;
}

int load_patterns(const char *filename, grok_pattern_node *patterns, size_t *count, size_t max_count) {
	FILE *f = fopen(filename, "r");
	if (!f) return -1;

	char line[65535];

	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#' || strlen(line) < 2) continue;
		char key[1024], val[65535];
		if (sscanf(line, "%1023s %65534[^\n]", key, val) == 2) {
			if (pattern_upsert(patterns, count, max_count, key, val) < 0)
				break;
		}
	}
	fclose(f);
	return 0;
}

static const char *find_pattern_regex(grok_pattern_node *patterns, int count, const char *key, int *found)
{
	*found = 0;
	for (int j = 0; j < count; j++) {
		if (strcmp(patterns[j].name, key) == 0) {
			*found = 1;
			return patterns[j].regex;
		}
	}
	return "(?!)"; /* unknown pattern: force match failure rather than silent .*? */
}

int expand_grok_pattern(char *src, string *dst_pass, grok_pattern_node *patterns, int count) {
	char *p = src;
	int expanded = 0;
	while (*p) {
		if (strncmp(p, "%{", 2) == 0) {
			p += 2;
			char key[256] = {0}, field[256] = {0};
			int i = 0;
			while (*p && *p != '}' && *p != ':') {
				if (i < (int)sizeof(key) - 1)
					key[i++] = *p;
				++p;
			}
			key[i] = '\0';

			if (*p == ':') {
				p++;
				i = 0;
				while (*p && *p != '}') {
					if (i < (int)sizeof(field) - 1)
						field[i++] = *p;
					++p;
				}
				field[i] = '\0';
			}

			if (*p == '}') p++;

			int found = 0;
			const char *regex = find_pattern_regex(patterns, count, key, &found);
			if (!found)
				glog(L_WARN, "grok: unknown pattern '%%{%s}' — match will fail until defined\n", key);

			/* Always keep named captures at every expansion depth (Elastic/grok_exporter). */
			if (field[0]) {
				char capture[256];
				grok_sanitize_capture_name(field, capture, sizeof(capture));
				string_sprintf(dst_pass, "(?<%s>%s)", capture, regex);
			}
			else
				string_sprintf(dst_pass, "(?:%s)", regex);

			++expanded;
		} else {
			string_cat(dst_pass, p++, 1);
		}
	}
	return expanded;
}

/* Reloaded pattern tables stay alive until grok_stop so expand never UAF. */
static grok_pattern *grok_patterns_retired;

static grok_pattern *grok_pattern_get(void)
{
	return __atomic_load_n(&ac->grok_patterns, __ATOMIC_ACQUIRE);
}

static void grok_pattern_retire(grok_pattern *gp)
{
	if (!gp)
		return;
	gp->retired_next = grok_patterns_retired;
	grok_patterns_retired = gp;
}

void grok_patterns_free_retired(void)
{
	while (grok_patterns_retired) {
		grok_pattern *gp = grok_patterns_retired;
		grok_patterns_retired = gp->retired_next;
		if (gp->nodes)
			free(gp->nodes);
		free(gp);
	}
}

int grok_expand_ok(string *src, string **dst, grok_pattern *patterns) {
	if (!src || !dst)
		return -1;

	if (!patterns)
		patterns = grok_pattern_get();
	if (!patterns) {
		glog(L_ERROR, "Don't loaded grok patterns\n");
		return -2; /* soft: retry when patterns appear */
	}

	int initialized = 0;
	string *src_pass = src;
	string *dst_pass = NULL;
	int expanded = 1;
	uint64_t pass;
	for (pass = 0; expanded != 0 && pass < GROK_EXPAND_MAX_PASSES; ++pass) {
		dst_pass = string_init(src_pass->l * 2 + 64);
		if (!dst_pass) {
			if (initialized)
				string_free(src_pass);
			return -1;
		}
		expanded = expand_grok_pattern(src_pass->s, dst_pass, patterns->nodes, (int)patterns->count);
		if (initialized)
			string_free(src_pass);
		src_pass = dst_pass;
		initialized = 1;
	}
	if (expanded) {
		glog(L_ERROR, "grok: pattern expansion exceeded %d passes (possible cycle)\n", GROK_EXPAND_MAX_PASSES);
		if (initialized)
			string_free(src_pass);
		*dst = NULL;
		return -1;
	}
	*dst = src_pass;
	return 0;
}

void grok_expand(string *src, string **dst, grok_pattern *patterns) {
	(void)grok_expand_ok(src, dst, patterns);
}

int grok_multimetric_hash_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((grok_multimetric_node*)obj)->key;

	return strcmp(s1, s2);
}

static const char *grok_separator(grok_ds *gds)
{
	return (gds && gds->separator && gds->separator->s) ? gds->separator->s : ", ";
}

static int grok_ensure_splited_lbl(struct grok_ctx_cb *ctx, uint64_t need)
{
	if (need == 0)
		return 0;
	if (!ctx->splited_lbl) {
		ctx->splited_lbl = calloc(need, sizeof(alligator_ht *));
		if (!ctx->splited_lbl)
			return -1;
		ctx->splited_lbl_size = need;
		for (uint64_t k = 0; k < need; ++k)
			ctx->splited_lbl[k] = alligator_ht_init(NULL);
		return 0;
	}
	if (need <= ctx->splited_lbl_size)
		return 0;
	alligator_ht **grown = realloc(ctx->splited_lbl, need * sizeof(alligator_ht *));
	if (!grown)
		return -1;
	for (uint64_t k = ctx->splited_lbl_size; k < need; ++k)
		grown[k] = alligator_ht_init(NULL);
	ctx->splited_lbl = grown;
	ctx->splited_lbl_size = need;
	return 0;
}

static int print_named_group(const char *name, size_t name_len, int beg, int end, void *arg)
{
	struct grok_ctx_cb *ctx = arg;
	(void)name_len;

	const char *line = ctx->line->s;

	if (beg < 0)
		return 0;

	size_t line_len = ctx->line->l;
	if (end < beg || (size_t)beg >= line_len)
		return 0;
	if ((size_t)end > line_len)
		end = (int)line_len;
	size_t key_len = (size_t)(end - beg);
	/* Cap capture copy to avoid multi-MB stack VLAs. */
	enum { GROK_CAPTURE_MAX = 65536 };
	size_t copy_len = key_len < GROK_CAPTURE_MAX ? key_len : GROK_CAPTURE_MAX;
	char *key = malloc(copy_len + 1);
	if (!key)
		return 0;
	memcpy(key, line + beg, copy_len);
	key[copy_len] = '\0';
	key_len = copy_len;
	char mname_buf[256];
	strlcpy(mname_buf, name, sizeof(mname_buf));
	char *mname = mname_buf;
	grok_multimetric_node *gmm_node = NULL;

	uint32_t hash = tommy_strhash_u32(0, mname);

	carglog(ctx->carg, L_TRACE, "processing: %s\n", mname);
	if (ctx->gds->gmm_splited_inherit_tag && (gmm_node = alligator_ht_search(ctx->gds->gmm_splited_inherit_tag, grok_multimetric_hash_compare, mname, hash))) {
		carglog(ctx->carg, L_TRACE, "\t>splited_inherit_tag\n");
		if (!ctx->splited_lbl_inherited)
			ctx->splited_lbl_inherited = alligator_ht_init(NULL);
		labels_hash_insert_nocache(ctx->splited_lbl_inherited, mname, key);
	}

	if (ctx->gn->value && !strcmp(mname, ctx->gn->value)) {
		carglog(ctx->carg, L_TRACE, "\t>simple\n");
		ctx->value_set = 1;
		ctx->value = strtod(key, NULL);
		goto done;
	}
	else if (!strcmp(mname, "__name__")) {
		carglog(ctx->carg, L_TRACE, "\t>metric with custom name\n");
		ctx->custom_name = 1;
		strlcpy(ctx->metric_name, key, sizeof(ctx->metric_name));
		goto done;
	}
	else if (ctx->gds->gmm_splited_tags && (gmm_node = alligator_ht_search(ctx->gds->gmm_splited_tags, grok_multimetric_hash_compare, mname, hash))) {
		carglog(ctx->carg, L_TRACE, "\t>splited_tags\n");
		if (!ctx->splited_tags_names)
			ctx->splited_tags_names = string_tokens_new();
		string_tokens_check_or_add(ctx->splited_tags_names, gmm_node->metric_name->s, gmm_node->metric_name->l);
		string_tokens *st = string_tokens_char_split_literal(key, key_len, (char *)grok_separator(ctx->gds));
		if (st) {
			if (grok_ensure_splited_lbl(ctx, st->l) == 0) {
				uint64_t n = st->l < ctx->splited_lbl_size ? st->l : ctx->splited_lbl_size;
				for (uint64_t k = 0; k < n; ++k)
					labels_hash_insert_nocache(ctx->splited_lbl[k], mname, st->str[k]->s);
			}
			string_tokens_free(st);
		}
		goto done;
	}
	else if (ctx->gds->gmm_splited_quantile && (gmm_node = alligator_ht_search(ctx->gds->gmm_splited_quantile, grok_multimetric_hash_compare, mname, hash))) {
		carglog(ctx->carg, L_TRACE, "\t>splited_quantile\n");
		if (!ctx->splited_quantile_names)
			ctx->splited_quantile_names = string_tokens_new();
		size_t nmetrics = alligator_ht_count(ctx->gds->gmm_splited_quantile);
		if (!ctx->splited_quantile_values) {
			ctx->splited_quantile_values = calloc(nmetrics, sizeof(double *));
			ctx->splited_quantile_counts = calloc(nmetrics, sizeof(uint64_t));
			if (!ctx->splited_quantile_values || !ctx->splited_quantile_counts) {
				free(ctx->splited_quantile_values);
				free(ctx->splited_quantile_counts);
				ctx->splited_quantile_values = NULL;
				ctx->splited_quantile_counts = NULL;
				goto done;
			}
		}
		int64_t index = (int64_t)string_tokens_check_or_add(ctx->splited_quantile_names, gmm_node->metric_name->s, gmm_node->metric_name->l);
		if (index > 0 && (size_t)index <= nmetrics) {
			string_tokens *st = string_tokens_char_split_literal(key, key_len, (char *)grok_separator(ctx->gds));
			if (st) {
				if (grok_ensure_splited_lbl(ctx, st->l) == 0) {
					free(ctx->splited_quantile_values[index - 1]);
					ctx->splited_quantile_values[index - 1] = calloc(st->l ? st->l : 1, sizeof(double));
					if (ctx->splited_quantile_values[index - 1]) {
						ctx->splited_quantile_counts[index - 1] = st->l;
						for (uint64_t k = 0; k < st->l; ++k)
							ctx->splited_quantile_values[index - 1][k] = strtod(st->str[k]->s, NULL);
					}
				}
				string_tokens_free(st);
			}
		}
		goto done;
	}
	else if (ctx->gds->gmm_splited_counter && (gmm_node = alligator_ht_search(ctx->gds->gmm_splited_counter, grok_multimetric_hash_compare, mname, hash))) {
		carglog(ctx->carg, L_TRACE, "\t>splited_counter\n");
		if (!ctx->splited_counter_names)
			ctx->splited_counter_names = string_tokens_new();
		size_t nmetrics = alligator_ht_count(ctx->gds->gmm_splited_counter);
		if (!ctx->splited_counter_values) {
			ctx->splited_counter_values = calloc(nmetrics, sizeof(int64_t *));
			ctx->splited_counter_counts = calloc(nmetrics, sizeof(uint64_t));
			if (!ctx->splited_counter_values || !ctx->splited_counter_counts) {
				free(ctx->splited_counter_values);
				free(ctx->splited_counter_counts);
				ctx->splited_counter_values = NULL;
				ctx->splited_counter_counts = NULL;
				goto done;
			}
		}
		int64_t index = (int64_t)string_tokens_check_or_add(ctx->splited_counter_names, gmm_node->metric_name->s, gmm_node->metric_name->l);
		if (index > 0 && (size_t)index <= nmetrics) {
			string_tokens *st = string_tokens_char_split_literal(key, key_len, (char *)grok_separator(ctx->gds));
			if (st) {
				if (grok_ensure_splited_lbl(ctx, st->l) == 0) {
					free(ctx->splited_counter_values[index - 1]);
					ctx->splited_counter_values[index - 1] = calloc(st->l ? st->l : 1, sizeof(int64_t));
					if (ctx->splited_counter_values[index - 1]) {
						ctx->splited_counter_counts[index - 1] = st->l;
						uint64_t n = st->l < ctx->splited_lbl_size ? st->l : ctx->splited_lbl_size;
						for (uint64_t k = 0; k < st->l; ++k) {
							ctx->splited_counter_values[index - 1][k] = strtoll(st->str[k]->s, NULL, 10);
							if (k < n)
								labels_hash_insert_nocache(ctx->splited_lbl[k], mname, st->str[k]->s);
						}
					}
				}
				string_tokens_free(st);
			}
		}
		goto done;
	}
	else if (ctx->gds->gmm_splited_le && (gmm_node = alligator_ht_search(ctx->gds->gmm_splited_le, grok_multimetric_hash_compare, mname, hash))) {
		carglog(ctx->carg, L_TRACE, "\t>splited_le\n");
		if (!ctx->splited_le_names)
			ctx->splited_le_names = string_tokens_new();
		size_t nmetrics = alligator_ht_count(ctx->gds->gmm_splited_le);
		if (!ctx->splited_le_values) {
			ctx->splited_le_values = calloc(nmetrics, sizeof(double *));
			ctx->splited_le_counts = calloc(nmetrics, sizeof(uint64_t));
			if (!ctx->splited_le_values || !ctx->splited_le_counts) {
				free(ctx->splited_le_values);
				free(ctx->splited_le_counts);
				ctx->splited_le_values = NULL;
				ctx->splited_le_counts = NULL;
				goto done;
			}
		}
		int64_t index = (int64_t)string_tokens_check_or_add(ctx->splited_le_names, gmm_node->metric_name->s, gmm_node->metric_name->l);
		if (index > 0 && (size_t)index <= nmetrics) {
			string_tokens *st = string_tokens_char_split_literal(key, key_len, (char *)grok_separator(ctx->gds));
			if (st) {
				if (grok_ensure_splited_lbl(ctx, st->l) == 0) {
					free(ctx->splited_le_values[index - 1]);
					ctx->splited_le_values[index - 1] = calloc(st->l ? st->l : 1, sizeof(double));
					if (ctx->splited_le_values[index - 1]) {
						ctx->splited_le_counts[index - 1] = st->l;
						for (uint64_t k = 0; k < st->l; ++k)
							ctx->splited_le_values[index - 1][k] = strtod(st->str[k]->s, NULL);
					}
				}
				string_tokens_free(st);
			}
		}
		goto done;
	}
	else if (ctx->gds->gmm_quantile && (gmm_node = alligator_ht_search(ctx->gds->gmm_quantile, grok_multimetric_hash_compare, mname, hash))) {
		carglog(ctx->carg, L_TRACE, "\t>quantile\n");
		if (!ctx->quantile_names)
			ctx->quantile_names = string_tokens_new();
		size_t nmetrics = alligator_ht_count(ctx->gds->gmm_quantile);
		if (!ctx->quantile_values)
			ctx->quantile_values = calloc(nmetrics, sizeof(double));
		int64_t index = (int64_t)string_tokens_check_or_add(ctx->quantile_names, gmm_node->metric_name->s, gmm_node->metric_name->l);
		if (ctx->quantile_values && index > 0 && (size_t)index <= nmetrics)
			ctx->quantile_values[index - 1] = strtod(key, NULL);
		goto done;
	}
	else if (ctx->gds->gmm_le && (gmm_node = alligator_ht_search(ctx->gds->gmm_le, grok_multimetric_hash_compare, mname, hash))) {
		carglog(ctx->carg, L_TRACE, "\t>le\n");
		if (!ctx->le_names)
			ctx->le_names = string_tokens_new();
		size_t nmetrics = alligator_ht_count(ctx->gds->gmm_le);
		if (!ctx->le_values)
			ctx->le_values = calloc(nmetrics, sizeof(double));
		int64_t index = (int64_t)string_tokens_check_or_add(ctx->le_names, gmm_node->metric_name->s, gmm_node->metric_name->l);
		if (ctx->le_values && index > 0 && (size_t)index <= nmetrics)
			ctx->le_values[index - 1] = strtod(key, NULL);
		goto done;
	}
	else if (ctx->gds->gmm_counter && (gmm_node = alligator_ht_search(ctx->gds->gmm_counter, grok_multimetric_hash_compare, mname, hash))) {
		carglog(ctx->carg, L_TRACE, "\t>counter\n");
		if (!ctx->counter_names)
			ctx->counter_names = string_tokens_new();
		size_t nmetrics = alligator_ht_count(ctx->gds->gmm_counter);
		if (!ctx->counter_values)
			ctx->counter_values = calloc(nmetrics, sizeof(double));
		int64_t index = (int64_t)string_tokens_check_or_add(ctx->counter_names, gmm_node->metric_name->s, gmm_node->metric_name->l);
		if (ctx->counter_values && index > 0 && (size_t)index <= nmetrics)
			ctx->counter_values[index - 1] = strtod(key, NULL);
		goto done;
	}
	else if (ctx->gds->gmm_gauge && (gmm_node = alligator_ht_search(ctx->gds->gmm_gauge, grok_multimetric_hash_compare, mname, hash))) {
		carglog(ctx->carg, L_TRACE, "\t>gauge\n");
		if (!ctx->gauge_names)
			ctx->gauge_names = string_tokens_new();
		size_t nmetrics = alligator_ht_count(ctx->gds->gmm_gauge);
		if (!ctx->gauge_values)
			ctx->gauge_values = calloc(nmetrics, sizeof(double));
		int64_t index = (int64_t)string_tokens_check_or_add(ctx->gauge_names, gmm_node->metric_name->s, gmm_node->metric_name->l);
		if (ctx->gauge_values && index > 0 && (size_t)index <= nmetrics)
			ctx->gauge_values[index - 1] = strtod(key, NULL);
		goto done;
	}
	metric_label_value_validator_normalizer(key, key_len);
	metric_name_normalizer(mname, strlen(mname));
	labels_hash_insert_nocache(ctx->lbl, mname, key);
done:
	free(key);
	return 0;
}

static int grok_compile_node(grok_node *gn, context_arg *carg)
{
	if (gn->reg || gn->compile_failed)
		return gn->reg ? 0 : -1;

	if (!gn->expanded_match) {
		if (carg)
			carglog(carg, L_ERROR, "Don't loaded grok patterns\n");
		else
			glog(L_ERROR, "Don't loaded grok patterns\n");
		gn->compile_failed = 1;
		return -1;
	}

	const char *pcreErrorStr = NULL;
	int pcreErrorOffset = 0;
	gn->reg = pcre_compile(gn->expanded_match->s, GROK_PCRE_OPTIONS, &pcreErrorStr, &pcreErrorOffset, NULL);
	if (!gn->reg) {
		pcreErrorStr = NULL;
		gn->reg = pcre_compile(gn->expanded_match->s, PCRE_DUPNAMES, &pcreErrorStr, &pcreErrorOffset, NULL);
	}
	if (!gn->reg) {
		if (carg)
			carglog(carg, L_ERROR, "PCRE grok compile failed at %d: %s: '%s'\n",
				pcreErrorOffset, pcreErrorStr ? pcreErrorStr : "unknown", gn->expanded_match->s);
		else
			glog(L_ERROR, "PCRE grok compile failed at %d: %s: '%s'\n",
				pcreErrorOffset, pcreErrorStr ? pcreErrorStr : "unknown", gn->expanded_match->s);
		gn->compile_failed = 1;
		return -1;
	}

	const char *studyErr = NULL;
#ifdef PCRE_STUDY_JIT_COMPILE
	gn->pcre_extra = pcre_study(gn->reg, PCRE_STUDY_JIT_COMPILE, &studyErr);
#else
	gn->pcre_extra = pcre_study(gn->reg, 0, &studyErr);
#endif
	if (studyErr) {
		if (carg)
			carglog(carg, L_WARN, "PCRE grok study: %s\n", studyErr);
		else
			glog(L_WARN, "PCRE grok study: %s\n", studyErr);
	}

#if defined(PCRE_EXTRA_MATCH_LIMIT)
	if (gn->pcre_extra) {
		gn->pcre_extra->flags |= PCRE_EXTRA_MATCH_LIMIT;
		gn->pcre_extra->match_limit = GROK_PCRE_MATCH_LIMIT;
	}
#endif

	pcre_fullinfo(gn->reg, gn->pcre_extra, PCRE_INFO_CAPTURECOUNT, &gn->capturecount);
	pcre_fullinfo(gn->reg, gn->pcre_extra, PCRE_INFO_NAMECOUNT, &gn->namecount);
	pcre_fullinfo(gn->reg, gn->pcre_extra, PCRE_INFO_NAMEENTRYSIZE, &gn->name_entry_size);
	{
		char *nametable = NULL;
		pcre_fullinfo(gn->reg, gn->pcre_extra, PCRE_INFO_NAMETABLE, &nametable);
		gn->nametable = nametable;
	}
	return 0;
}

static void grok_ctx_cleanup(grok_ctx_cb *ctx)
{
	if (ctx->splited_lbl_size) {
		for (uint64_t k = 0; k < ctx->splited_lbl_size; ++k)
			labels_hash_free(ctx->splited_lbl[k]);
		free(ctx->splited_lbl);
		ctx->splited_lbl = NULL;
		ctx->splited_lbl_size = 0;
	}
	free(ctx->le_values);
	ctx->le_values = NULL;
	free(ctx->quantile_values);
	ctx->quantile_values = NULL;
	free(ctx->counter_values);
	ctx->counter_values = NULL;
	free(ctx->gauge_values);
	ctx->gauge_values = NULL;

	if (ctx->splited_quantile_values) {
		size_t n = ctx->splited_quantile_names ? ctx->splited_quantile_names->l : 0;
		for (size_t i = 0; i < n; ++i)
			free(ctx->splited_quantile_values[i]);
		free(ctx->splited_quantile_values);
		ctx->splited_quantile_values = NULL;
	}
	free(ctx->splited_quantile_counts);
	ctx->splited_quantile_counts = NULL;

	if (ctx->splited_le_values) {
		size_t n = ctx->splited_le_names ? ctx->splited_le_names->l : 0;
		for (size_t i = 0; i < n; ++i)
			free(ctx->splited_le_values[i]);
		free(ctx->splited_le_values);
		ctx->splited_le_values = NULL;
	}
	free(ctx->splited_le_counts);
	ctx->splited_le_counts = NULL;

	if (ctx->splited_counter_values) {
		size_t n = ctx->splited_counter_names ? ctx->splited_counter_names->l : 0;
		for (size_t i = 0; i < n; ++i)
			free(ctx->splited_counter_values[i]);
		free(ctx->splited_counter_values);
		ctx->splited_counter_values = NULL;
	}
	free(ctx->splited_counter_counts);
	ctx->splited_counter_counts = NULL;

	if (ctx->le_names) { string_tokens_free(ctx->le_names); ctx->le_names = NULL; }
	if (ctx->quantile_names) { string_tokens_free(ctx->quantile_names); ctx->quantile_names = NULL; }
	if (ctx->counter_names) { string_tokens_free(ctx->counter_names); ctx->counter_names = NULL; }
	if (ctx->gauge_names) { string_tokens_free(ctx->gauge_names); ctx->gauge_names = NULL; }
	if (ctx->splited_le_names) { string_tokens_free(ctx->splited_le_names); ctx->splited_le_names = NULL; }
	if (ctx->splited_quantile_names) { string_tokens_free(ctx->splited_quantile_names); ctx->splited_quantile_names = NULL; }
	if (ctx->splited_counter_names) { string_tokens_free(ctx->splited_counter_names); ctx->splited_counter_names = NULL; }
	if (ctx->splited_tags_names) { string_tokens_free(ctx->splited_tags_names); ctx->splited_tags_names = NULL; }
	if (ctx->splited_lbl_inherited) {
		labels_hash_free(ctx->splited_lbl_inherited);
		ctx->splited_lbl_inherited = NULL;
	}
	free(ctx->ovector);
	ctx->ovector = NULL;
}

void grok_handler_callback(void *funcarg, void* arg)
{
	grok_ctx_cb *ctx = (grok_ctx_cb*)funcarg;
	grok_node *gn = arg;

	/* Per-rule state must not leak across rules in the same context. */
	ctx->value = 0;
	ctx->value_set = 0;
	ctx->custom_name = 0;
	ctx->metric_name[0] = '\0';
	ctx->lbl = NULL;
	ctx->splited_lbl = NULL;
	ctx->splited_lbl_size = 0;
	ctx->splited_lbl_inherited = NULL;
	ctx->quantile_values = NULL;
	ctx->le_values = NULL;
	ctx->counter_values = NULL;
	ctx->gauge_values = NULL;
	ctx->splited_quantile_values = NULL;
	ctx->splited_quantile_counts = NULL;
	ctx->splited_le_values = NULL;
	ctx->splited_le_counts = NULL;
	ctx->splited_counter_values = NULL;
	ctx->splited_counter_counts = NULL;
	ctx->quantile_names = NULL;
	ctx->le_names = NULL;
	ctx->counter_names = NULL;
	ctx->gauge_names = NULL;
	ctx->splited_quantile_names = NULL;
	ctx->splited_le_names = NULL;
	ctx->splited_counter_names = NULL;
	ctx->splited_tags_names = NULL;
	ctx->ovector = NULL;

	if (grok_compile_node(gn, ctx->carg) < 0)
		return;

	int ovecsize = (gn->capturecount + 1) * 3;
	if (ovecsize < 30)
		ovecsize = 30;
	ctx->ovector = malloc(sizeof(int) * (size_t)ovecsize);
	if (!ctx->ovector)
		return;

	/* Unanchored patterns bumpalong from offset 0 (PCRE / grok_exporter behaviour). */
	int exec_opts = PCRE_NO_UTF8_CHECK;
	int r = pcre_exec(gn->reg, gn->pcre_extra, ctx->line->s, (int)ctx->line->l, 0, exec_opts, ctx->ovector, ovecsize);
	carglog(ctx->carg, L_TRACE, "matching line len %zu (r: %d) line %.*s pattern %.*s\n",
		ctx->line->l, r, 80, ctx->line->s, 80, gn->expanded_match->s);

	ctx->gn = gn;

	if (r >= 0) {
		carglog(ctx->carg, L_DEBUG, "++++ grok_handler_callback loop ++++\n");
		ctx->lbl = alligator_ht_init(NULL);
		for (int i = 0; i < gn->namecount; i++) {
			const unsigned char *entry = (const unsigned char *)gn->nametable + i * gn->name_entry_size;
			int gnum = (entry[0] << 8) | entry[1];
			const char *name = (const char *)(entry + 2);
			if (gnum < 1 || gnum > gn->capturecount)
				continue;
			print_named_group(name, strlen(name), ctx->ovector[2 * gnum], ctx->ovector[2 * gnum + 1], ctx);
		}

		/* Only emit metrics for fields that were actually captured on this line. */
		if (ctx->le_names && ctx->le_values) {
			for (uint64_t i = 0; i < ctx->le_names->l; ++i) {
				alligator_ht *hash = labels_dup(ctx->lbl);
				metric_update(ctx->le_names->str[i]->s, hash, &ctx->le_values[i], DATATYPE_DOUBLE, ctx->carg);
			}
		}
		if (ctx->counter_names && ctx->counter_values) {
			for (uint64_t i = 0; i < ctx->counter_names->l; ++i) {
				alligator_ht *hash = labels_dup(ctx->lbl);
				metric_update(ctx->counter_names->str[i]->s, hash, &ctx->counter_values[i], DATATYPE_DOUBLE, ctx->carg);
			}
		}
		if (ctx->gauge_names && ctx->gauge_values) {
			for (uint64_t i = 0; i < ctx->gauge_names->l; ++i) {
				alligator_ht *hash = labels_dup(ctx->lbl);
				/* Last-value: set, do not accumulate. */
				metric_add(ctx->gauge_names->str[i]->s, hash, &ctx->gauge_values[i], DATATYPE_DOUBLE, ctx->carg);
			}
		}
		if (ctx->quantile_names && ctx->quantile_values) {
			for (uint64_t i = 0; i < ctx->quantile_names->l; ++i) {
				alligator_ht *hash = labels_dup(ctx->lbl);
				metric_update(ctx->quantile_names->str[i]->s, hash, &ctx->quantile_values[i], DATATYPE_DOUBLE, ctx->carg);
			}
		}
		if (ctx->splited_counter_names && ctx->splited_counter_values) {
			for (uint64_t i = 0; i < ctx->splited_counter_names->l; ++i) {
				uint64_t n = ctx->splited_counter_counts ? ctx->splited_counter_counts[i] : 0;
				uint64_t lim = n < ctx->splited_lbl_size ? n : ctx->splited_lbl_size;
				for (uint64_t k = 0; k < lim; ++k) {
					alligator_ht *hash = labels_dup(ctx->splited_lbl[k]);
					labels_merge(hash, ctx->splited_lbl_inherited);
					metric_update(ctx->splited_counter_names->str[i]->s, hash, &ctx->splited_counter_values[i][k], DATATYPE_INT, ctx->carg);
				}
				char count_name[1024];
				snprintf(count_name, sizeof(count_name), "%s_count", ctx->splited_counter_names->str[i]->s);
				alligator_ht *hash = labels_dup(ctx->lbl);
				uint64_t count_val = n;
				metric_update(count_name, hash, &count_val, DATATYPE_UINT, ctx->carg);
			}
		}
		if (ctx->splited_le_names && ctx->splited_le_values) {
			for (uint64_t i = 0; i < ctx->splited_le_names->l; ++i) {
				uint64_t n = ctx->splited_le_counts ? ctx->splited_le_counts[i] : 0;
				uint64_t lim = n < ctx->splited_lbl_size ? n : ctx->splited_lbl_size;
				for (uint64_t k = 0; k < lim; ++k) {
					alligator_ht *hash = labels_dup(ctx->splited_lbl[k]);
					labels_merge(hash, ctx->splited_lbl_inherited);
					metric_update(ctx->splited_le_names->str[i]->s, hash, &ctx->splited_le_values[i][k], DATATYPE_DOUBLE, ctx->carg);
				}
			}
		}
		if (ctx->splited_quantile_names && ctx->splited_quantile_values) {
			for (uint64_t i = 0; i < ctx->splited_quantile_names->l; ++i) {
				uint64_t n = ctx->splited_quantile_counts ? ctx->splited_quantile_counts[i] : 0;
				uint64_t lim = n < ctx->splited_lbl_size ? n : ctx->splited_lbl_size;
				for (uint64_t k = 0; k < lim; ++k) {
					alligator_ht *hash = labels_dup(ctx->splited_lbl[k]);
					labels_merge(hash, ctx->splited_lbl_inherited);
					metric_update(ctx->splited_quantile_names->str[i]->s, hash, &ctx->splited_quantile_values[i][k], DATATYPE_DOUBLE, ctx->carg);
				}
			}
		}

		if (!ctx->value_set)
			ctx->value = 1;

		char *pub_name = gn->name;
		if (ctx->custom_name && ctx->metric_name[0])
			pub_name = ctx->metric_name;
		carglog(ctx->carg, L_TRACE, "add metric %s = %f\n", pub_name, ctx->value);

		if (ctx->carg && ctx->carg->log_ch_out && ctx->lbl) {
			json_t *doc = labels_to_json(ctx->lbl);
			if (doc) {
				if (!json_object_get(doc, "message") && ctx->line && ctx->line->s)
					json_object_set_new(doc, "message",
						json_stringn(ctx->line->s, ctx->line->l));
				carg_emit_log_document(ctx->carg, doc);
				json_decref(doc);
			}
		}

		if (gn->is_gauge)
			metric_add(pub_name, ctx->lbl, &ctx->value, DATATYPE_DOUBLE, ctx->carg);
		else
			metric_update(pub_name, ctx->lbl, &ctx->value, DATATYPE_DOUBLE, ctx->carg);
		ctx->lbl = NULL; /* consumed by metric_add / metric_update */
	} else if (r != PCRE_ERROR_NOMATCH) {
		carglog(ctx->carg, L_TRACE, "matching line ERROR: pcre_exec %d\n", r);
	}

	grok_ctx_cleanup(ctx);
}

/* funcarg: optional int* soft_fail — set when expand fails (patterns not ready). */
void grok_handler_initial_patterns(void *funcarg, void* arg)
{
	int *soft_fail = funcarg;
	grok_node *gn = arg;
	if (gn->expanded_match) {
		string_free(gn->expanded_match);
		gn->expanded_match = NULL;
	}
	if (gn->pcre_extra) {
		pcre_free_study(gn->pcre_extra);
		gn->pcre_extra = NULL;
	}
	if (gn->reg) {
		pcre_free(gn->reg);
		gn->reg = NULL;
	}
	gn->compile_failed = 0;
	gn->nametable = NULL;
	gn->capturecount = 0;
	gn->namecount = 0;
	gn->name_entry_size = 0;
	{
		int er = grok_expand_ok(gn->match, &gn->expanded_match, NULL);
		if (er < 0) {
			gn->compile_failed = 1;
			/* -2: patterns not loaded yet — defer have_epoch. Else permanent. */
			if (er == -2 && soft_fail)
				(*soft_fail)++;
			return;
		}
	}
	/* Compile once under the caller's gds wrlock (see grok_handler).
	 * Permanent bad regex sets compile_failed; still publish have_epoch. */
	grok_compile_node(gn, NULL);
}

typedef struct grok_ml_ud {
	grok_ds *gds;
	context_arg *carg;
	string *line;
	grok_ctx_cb *ctx;
} grok_ml_ud;

static void grok_record_cb(void *vud, const char *record, size_t len)
{
	grok_ml_ud *ud = vud;
	memset(ud->ctx, 0, sizeof(*ud->ctx));
	ud->ctx->gds = ud->gds;
	ud->ctx->carg = ud->carg;
	ud->ctx->line = ud->line;
	string_null(ud->line);
	string_cat(ud->line, (char *)record, len);
	alligator_ht_foreach_arg(ud->gds->hash, grok_handler_callback, ud->ctx);
}

void grok_handler(char *metrics, size_t size, context_arg *carg)
{
	if (!metrics || !size)
		return;

	grok_ds* gds = grok_get(carg->name);
	if (!gds) {
		carglog(carg, L_ERROR, "not found grok keys with key '%s' for '%s'\n", carg->name, carg->key);
		carg->parser_status = 0;
		return;
	}
	string *line = string_new();

	pthread_rwlock_rdlock(&gds->lock);
	{
		uint32_t want = __atomic_load_n(&gds->want_epoch, __ATOMIC_ACQUIRE);
		uint32_t have = __atomic_load_n(&gds->have_epoch, __ATOMIC_ACQUIRE);
		if (have != want) {
			pthread_rwlock_unlock(&gds->lock);
			pthread_rwlock_wrlock(&gds->lock);
			/* Loop until want stable after recompile, or soft expand fail defers have. */
			for (;;) {
				want = __atomic_load_n(&gds->want_epoch, __ATOMIC_RELAXED);
				have = __atomic_load_n(&gds->have_epoch, __ATOMIC_RELAXED);
				if (have == want)
					break;
				int soft_fail = 0;
				alligator_ht_foreach_arg(gds->hash, grok_handler_initial_patterns, &soft_fail);
				uint32_t want_after = __atomic_load_n(&gds->want_epoch, __ATOMIC_RELAXED);
				if (soft_fail)
					break; /* patterns not ready — leave have mismatch for retry */
				if (want_after == want) {
					__atomic_store_n(&gds->have_epoch, want, __ATOMIC_RELEASE);
					break;
				}
				/* want bumped mid-recompile (reload) — re-expand against new epoch */
			}
			pthread_rwlock_unlock(&gds->lock);
			pthread_rwlock_rdlock(&gds->lock);
		}
	}

	/* Own mapping copy from gds; refresh/clear when key was recreated (mm_epoch).
	 * Leave carg->mm alone if it came from API (grok_mm_epoch still 0). */
	if (carg->grok_mm_epoch) {
		if (carg->grok_mm_epoch != gds->mm_epoch) {
			if (carg->mm)
				mapping_free_recurse(carg->mm);
			carg->mm = gds->mm ? mapping_copy(gds->mm) : NULL;
			carg->grok_mm_epoch = gds->mm ? gds->mm_epoch : 0;
		}
	} else if (!carg->mm && gds->mm) {
		carg->mm = mapping_copy(gds->mm);
		carg->grok_mm_epoch = gds->mm_epoch;
	}

	grok_ctx_cb *ctx = calloc(1, sizeof(*ctx));
	grok_ml_ud ud = {
		.gds = gds,
		.carg = carg,
		.line = line,
		.ctx = ctx,
	};

	carg_linebuf_ensure(carg);
	alligator_linebuf_feed(&carg->ml_lb, metrics, size, grok_record_cb, &ud);

	pthread_rwlock_unlock(&gds->lock);
	free(ctx);
	string_free(line);
	grok_put(gds);
	carg->parser_status = 1;
}

static void grok_mark_reapply(void *arg)
{
	grok_ds *gps = arg;
	if (!gps || gps->zombie)
		return;
	__atomic_fetch_add(&gps->want_epoch, 1, __ATOMIC_RELEASE);
}

int grok_patterns_init() {
	if (!ac->grok_patterns_path || !ac->grok_patterns_path->l)
		return 1;

	size_t pat_count = 0;
	for (uint64_t i = 0; i < ac->grok_patterns_path->l; ++i) {
		pat_count += count_file_lines(ac->grok_patterns_path->str[i]->s);
	}
	if (!pat_count)
		pat_count = 1;

	grok_pattern *gp = calloc(1, sizeof(*gp));
	grok_pattern_node *patterns = calloc(pat_count, sizeof(*patterns));
	size_t loaded = 0;
	for (uint64_t i = 0; i < ac->grok_patterns_path->l; ++i) {
		if (load_patterns(ac->grok_patterns_path->str[i]->s, patterns, &loaded, pat_count)) {
			glog(L_ERROR, "Failed to load patterns from '%s'\n", ac->grok_patterns_path->str[i]->s);
			free(patterns);
			free(gp);
			return 1;
		}
	}
	gp->nodes = patterns;
	gp->count = loaded;

	grok_pattern *old = __atomic_exchange_n(&ac->grok_patterns, gp, __ATOMIC_ACQ_REL);
	grok_pattern_retire(old);

	/* Bump want_epoch under life so foreach cannot race del destroy. */
	grok_life_lock();
	if (ac->grok)
		alligator_ht_foreach(ac->grok, grok_mark_reapply);
	grok_life_unlock();

	return 0;
}

string* grok_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	(void)arg;
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
