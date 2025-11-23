#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <oniguruma.h>
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

void expand_grok_pattern(const char *src, char *dst, grok_pattern_node *patterns, int count) {
    const char *p = src;
    char *out = dst;
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

            if (strlen(field) > 0)
                out += sprintf(out, "(?<%s>%s)", field, regex);
            else
                out += sprintf(out, "(?:%s)", regex);
        } else {
            *out++ = *p++;
        }
    }
    *out = '\0';
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
            if (!strncmp(mname, "value", 5)) {
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
            //       (int)(name_end - name), (const char *)name,
            //       region->end[gnum] - region->beg[gnum],
            //       line + region->beg[gnum]);
        }
    }
    return 0;
}

void grok_expand(string *src, string **dst, grok_pattern *patterns) {
    if (!patterns) {
        glog(L_ERROR, "No loaded grok patterns");
        return;
    }
    *dst = string_init(src->l * 2);
    expand_grok_pattern(src->s, (*dst)->s, patterns->nodes, patterns->count);
    (*dst)->l = strlen((*dst)->s);
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

    regex_t *reg;
    OnigErrorInfo einfo;

    int r = onig_new(&reg, (UChar *)gn->expanded_match->s, (UChar *)(gn->expanded_match->s + gn->expanded_match->l), ONIG_OPTION_NONE, ONIG_ENCODING_UTF8, ONIG_SYNTAX_DEFAULT, &einfo);
    if (r != ONIG_NORMAL)
        return;

    OnigRegion *region = onig_region_new();
    r = onig_search(reg, (UChar *)ctx->line->s, (UChar *)(ctx->line->s + ctx->line->l), (UChar *)ctx->line->s, (UChar *)(ctx->line->s + ctx->line->l), region, ONIG_OPTION_NONE);

    ctx->region = region;

    if (r >= 0) {
	    ctx->lbl = alligator_ht_init(NULL);
        onig_foreach_name(reg, print_named_group, ctx);
        onig_region_free(ctx->region, 1);
        if (!ctx->value_set)
            ctx->value = 1;
        metric_update(gn->name, ctx->lbl, &ctx->value, DATATYPE_DOUBLE, NULL);
        onig_free(reg);
        return;
    }

    onig_region_free(region, 1);
    onig_free(reg);
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

    grok_ctx_cb *ctx = malloc(sizeof(*ctx));
    for (uint64_t i = 0; i < size; ) {
        size_t len = strcspn(metrics+i, "\n");
        len += strspn(metrics+i, "\n");
        memset(ctx, 0, sizeof(*ctx));
        ctx->line = line;
        string_cat(ctx->line, metrics+i, len);
        ctx->gds = gds;
        alligator_ht_foreach_arg(gds->hash, grok_handler_callback, ctx);

        string_null(ctx->line);

		i += len;
    }

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
