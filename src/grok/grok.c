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
#include "common/http.h"

#define MAX_PATTERNS 1024
#define MAX_LINE_LEN 1024
#define MAX_REGEX_LEN 2048

extern aconf *ac;

typedef struct grok_ctx_cb {
	string *line;
	OnigRegion *region;
	alligator_ht *lbl;
	double value;
	int value_set;
	char metric_name[255];
	grok_ds* gds;
	grok_node *gn;
	context_arg *carg;
} grok_ctx_cb;

int load_patterns(const char *filename, grok_pattern_node *patterns, size_t *count) {
	FILE *f = fopen(filename, "r");
	if (!f) return -1;

	char line[512];

	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#' || strlen(line) < 2) continue;
		char key[64], val[256];
		if (sscanf(line, "%63s %255[^\n]", key, val) == 2) {
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
			strlcpy(key, (char*)line + region->beg[gnum], key_len+1);
			if (ctx->gn->value && !strcmp(mname, ctx->gn->value)) {
				ctx->value_set = 1;
				ctx->value = strtod(key, NULL);
				continue;
			}
			else if (!strncmp(mname, "__name__", 5)) {
				ctx->value_set = 1;
				strlcpy(ctx->metric_name, key, 255);
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
		printf("onig new is %d, normal %d: '%s'\n", r, ONIG_NORMAL, gn->expanded_match->s);
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
		ctx->lbl = alligator_ht_init(NULL);
		onig_foreach_name(gn->reg, print_named_group, ctx);
		if (!ctx->value_set)
			ctx->value = 1;
		metric_update(gn->name, ctx->lbl, &ctx->value, DATATYPE_DOUBLE, NULL);
		//onig_free(reg);
	} else if (r == ONIG_MISMATCH) {
	} else {
		char s[ONIG_MAX_ERROR_MESSAGE_LEN];
		onig_error_code_to_str((UChar* )s, r);
		carglog(ctx->carg, L_TRACE, "matching line ERROR: %s\n", s);
		return;
	}

	onig_region_free(ctx->region, 1);
	//onig_free(reg);
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
