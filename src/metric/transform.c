#include "metric/namespace.h"
#include "action/type.h"
#include "main.h"
#include "common/logs.h"
#include <ctype.h>
#include <stdarg.h>
#include <strings.h>

static void metric_transform_info(context_arg *carg, action_node *an, const char *fmt, ...)
{
	int level;
	if (carg)
		level = carg->log_level;
	else if (an)
		level = an->log_level;
	else
		return;
	if (level < L_INFO)
		return;
	va_list ap;
	va_start(ap, fmt);
	wrlog(level, L_INFO, fmt, ap);
	va_end(ap);
}

static int metric_transform_append(char **dst, size_t *len, size_t *cap, const char *src, size_t src_len)
{
    if (!src_len)
        return 1;

    if (*len + src_len + 1 > *cap)
    {
        size_t new_cap = *cap ? *cap : 64;
        while (*len + src_len + 1 > new_cap)
            new_cap *= 2;

        char *tmp = realloc(*dst, new_cap);
        if (!tmp)
            return 0;
        *dst = tmp;
        *cap = new_cap;
    }

    memcpy(*dst + *len, src, src_len);
    *len += src_len;
    (*dst)[*len] = '\0';
    return 1;
}

char* metric_transform_name(char *name, action_node *an)
{
    if (!name || !an)
        return NULL;

    if (!an->metric_name_transform_pattern || !an->metric_name_transform_replacement)
        return NULL;

    size_t name_len = strlen(name);
    char *new_name = malloc(name_len + 1);
    if (!new_name)
        return NULL;

    strcpy(new_name, name);

    if (!an->metric_name_transform_pattern || !an->metric_name_transform_replacement)
        return new_name;

    if (!an->metric_name_transform_compiled)
    {
        const char *error = NULL;
        int erroffset = 0;
        an->metric_name_transform_compiled = pcre_compile(
            an->metric_name_transform_pattern,
            0,
            &error,
            &erroffset,
            NULL
        );
    }

    if (!an->metric_name_transform_compiled)
        return new_name;

    int ovector[90];
    int rc = pcre_exec(an->metric_name_transform_compiled, NULL, name, name_len, 0, 0, ovector, 90);
    if (rc < 0)
        return new_name;

    size_t result_len = 0;
    size_t result_cap = name_len + 1;
    char *result = malloc(result_cap);
    if (!result)
        return new_name;
    result[0] = '\0';

    char *replacement = an->metric_name_transform_replacement;
    size_t replacement_len = strlen(replacement);

    for (size_t i = 0; i < replacement_len; ++i)
    {
        if (replacement[i] == '$' && i + 1 < replacement_len && isdigit((unsigned char)replacement[i + 1]))
        {
            int group = 0;
            size_t j = i + 1;
            while (j < replacement_len && isdigit((unsigned char)replacement[j]))
            {
                group = group * 10 + (replacement[j] - '0');
                ++j;
            }

            if ((rc == 0 && group == 0) || (rc > 0 && group < rc))
            {
                int start = ovector[group * 2];
                int end = ovector[group * 2 + 1];
                if (start >= 0 && end >= start)
                {
                    if (!metric_transform_append(&result, &result_len, &result_cap, name + start, (size_t)(end - start)))
                    {
                        free(result);
                        return new_name;
                    }
                }
            }

            i = j - 1;
            continue;
        }

        if (!metric_transform_append(&result, &result_len, &result_cap, replacement + i, 1))
        {
            free(result);
            return new_name;
        }
    }

    if (strcmp(name, result) != 0)
        metric_transform_info(NULL, an, "metricstransform: metric name '%s' -> '%s'\n", name, result);
    free(new_name);
    return result;
}

char *metric_transform_alt_for_include(const char *export_name, const char *tree_metric_key)
{
    if (!export_name || !tree_metric_key)
        return NULL;
    return strcmp(export_name, tree_metric_key) ? (char *)tree_metric_key : NULL;
}

static int metric_transform_json_bool(json_t *val)
{
    if (!val)
        return 0;

    if (json_is_true(val))
        return 1;
    if (json_is_false(val))
        return 0;
    if (json_is_integer(val))
        return json_integer_value(val) != 0;
    if (json_is_string(val))
    {
        const char *s = json_string_value(val);
        return s && (!strcasecmp(s, "true") || !strcmp(s, "1"));
    }
    return 0;
}

static int metric_transform_match_pattern(const char *value, const char *pattern, int regexp)
{
    if (!pattern || !*pattern)
        return 1;
    if (!value)
        value = "";

    if (!regexp)
        return !strcmp(value, pattern);

    const char *error = NULL;
    int erroffset = 0;
    pcre *re = pcre_compile(pattern, 0, &error, &erroffset, NULL);
    if (!re)
        return 0;

    int ovector[30];
    int rc = pcre_exec(re, NULL, value, (int)strlen(value), 0, 0, ovector, 30);
    pcre_free(re);
    return rc >= 0;
}

static char* metric_transform_replace_groups(const char *source, const int *ovector, int rc, const char *replacement)
{
    size_t source_len = source ? strlen(source) : 0;
    if (!replacement)
        replacement = "";
    if (!source)
        source = "";

    size_t out_cap = source_len + strlen(replacement) + 8;
    char *out = calloc(1, out_cap);
    size_t out_len = 0;
    if (!out)
        return NULL;

    size_t replacement_len = strlen(replacement);
    for (size_t i = 0; i < replacement_len; ++i)
    {
        if (replacement[i] == '$' && i + 1 < replacement_len && isdigit((unsigned char)replacement[i + 1]))
        {
            int group = 0;
            size_t j = i + 1;
            while (j < replacement_len && isdigit((unsigned char)replacement[j]))
            {
                group = group * 10 + (replacement[j] - '0');
                ++j;
            }

            if (group < rc)
            {
                int start = ovector[group * 2];
                int end = ovector[group * 2 + 1];
                if (start >= 0 && end >= start)
                {
                    if (!metric_transform_append(&out, &out_len, &out_cap, source + start, (size_t)(end - start)))
                    {
                        free(out);
                        return NULL;
                    }
                }
            }
            i = j - 1;
            continue;
        }

        if (!metric_transform_append(&out, &out_len, &out_cap, replacement + i, 1))
        {
            free(out);
            return NULL;
        }
    }

    return out;
}

static char* metric_transform_replace_regex_once(const char *value, const char *regex, const char *replacement)
{
    if (!value || !regex)
        return NULL;

    const char *error = NULL;
    int erroffset = 0;
    pcre *re = pcre_compile(regex, 0, &error, &erroffset, NULL);
    if (!re)
        return NULL;

    int ovector[90];
    int val_len = (int)strlen(value);
    int rc = pcre_exec(re, NULL, value, val_len, 0, 0, ovector, 90);
    if (rc < 0)
    {
        pcre_free(re);
        return NULL;
    }

    char *replacement_text = metric_transform_replace_groups(value, ovector, rc, replacement);
    if (!replacement_text)
    {
        pcre_free(re);
        return NULL;
    }

    size_t out_len = strlen(value) + strlen(replacement_text) + 1;
    char *out = calloc(1, out_len);
    if (!out)
    {
        free(replacement_text);
        pcre_free(re);
        return NULL;
    }

    int start = ovector[0];
    int end = ovector[1];
    memcpy(out, value, (size_t)start);
    strcat(out, replacement_text);
    strcat(out, value + end);

    free(replacement_text);
    pcre_free(re);
    return out;
}

static char* metric_transform_replace_regex_all(const char *value, const char *regex, const char *replacement)
{
    if (!value || !regex)
        return NULL;

    char *current = strdup(value);
    if (!current)
        return NULL;

    while (1)
    {
        char *updated = metric_transform_replace_regex_once(current, regex, replacement);
        if (!updated)
            break;
        free(current);
        current = updated;
    }

    return current;
}

typedef struct metric_transform_update_ctx
{
    const char *metric_name;
    json_t *operation;
    int metric_regexp;
    const char *metric_pattern;
    context_arg *carg;
    action_node *an;
} metric_transform_update_ctx;

static void metric_transform_apply_operation(void *funcarg, void *arg)
{
    metric_transform_update_ctx *ctx = funcarg;
    labels_container *labelscont = arg;
    if (!ctx || !labelscont || !ctx->operation)
        return;

    const char *label = json_string_value(json_object_get(ctx->operation, "label"));
    const char *label_regex = json_string_value(json_object_get(ctx->operation, "label_regex"));
    int label_is_regexp = label_regex && *label_regex;

    if (label_is_regexp)
    {
        if (!metric_transform_match_pattern(labelscont->name, label_regex, 1))
            return;
    }
    else if (label && *label)
    {
        if (strcmp(labelscont->name, label))
            return;
    }
    else
        return;

    json_t *value_actions = json_object_get(ctx->operation, "value_actions");
    if (!value_actions || !json_is_array(value_actions))
        return;

    char *orig_val = strdup(labelscont->key ? labelscont->key : "");
    if (!orig_val)
        return;
    char *curr = strdup(orig_val);
    if (!curr)
    {
        free(orig_val);
        return;
    }

    size_t value_actions_size = json_array_size(value_actions);
    for (size_t i = 0; i < value_actions_size; ++i)
    {
        json_t *value_action = json_array_get(value_actions, i);
        const char *regex = json_string_value(json_object_get(value_action, "regex"));
        if (!regex || !*regex)
            continue;

        const char *replacement = json_string_value(json_object_get(value_action, "replacement"));
        if (!replacement)
            replacement = json_string_value(json_object_get(value_action, "new_value"));

        int replace_all = metric_transform_json_bool(json_object_get(value_action, "replace_all"));
        char *new_value = replace_all
            ? metric_transform_replace_regex_all(curr, regex, replacement)
            : metric_transform_replace_regex_once(curr, regex, replacement);
        if (!new_value)
            continue;

        free(curr);
        curr = new_value;
    }

    if (strcmp(orig_val, curr) != 0)
        metric_transform_info(ctx->carg, ctx->an,
            "metricstransform: metric '%s' label '%s' value '%s' -> '%s'\n",
            ctx->metric_name ? ctx->metric_name : "?",
            labelscont->name ? labelscont->name : "?",
            orig_val, curr);
    free(orig_val);

    if (labelscont->allocatedkey)
        free(labelscont->key);
    labelscont->key = curr;
    labelscont->allocatedkey = 1;
}

void metric_transform_labels(char *metric_name, char *metric_name_alt, alligator_ht *labels, json_t *metricstransform, context_arg *carg, action_node *an)
{
    if (!metric_name || !labels || !metricstransform)
        return;

    json_t *transforms = NULL;
    if (json_is_array(metricstransform))
        transforms = metricstransform;
    else if (json_is_object(metricstransform))
        transforms = json_object_get(metricstransform, "transforms");

    if (!transforms || !json_is_array(transforms))
        return;

    size_t transforms_size = json_array_size(transforms);
    for (size_t i = 0; i < transforms_size; ++i)
    {
        json_t *transform = json_array_get(transforms, i);
        if (!transform || !json_is_object(transform))
            continue;

        const char *metric_pattern = json_string_value(json_object_get(transform, "include"));
        if (!metric_pattern)
            metric_pattern = json_string_value(json_object_get(transform, "metric"));
        const char *match_type = json_string_value(json_object_get(transform, "match_type"));
        int metric_regexp = match_type && !strcasecmp(match_type, "regexp");
        if (json_object_get(transform, "metric_regex"))
        {
            metric_pattern = json_string_value(json_object_get(transform, "metric_regex"));
            metric_regexp = 1;
        }

        int matched = metric_transform_match_pattern(metric_name, metric_pattern, metric_regexp);
        if (!matched && metric_name_alt && *metric_name_alt)
            matched = metric_transform_match_pattern(metric_name_alt, metric_pattern, metric_regexp);
        if (!matched)
            continue;

        json_t *operations = json_object_get(transform, "operations");
        if (!operations || !json_is_array(operations))
            continue;

        size_t operations_size = json_array_size(operations);
        for (size_t j = 0; j < operations_size; ++j)
        {
            json_t *operation = json_array_get(operations, j);
            const char *action = json_string_value(json_object_get(operation, "action"));
            if (!action || strcmp(action, "update_label"))
                continue;

            metric_transform_update_ctx ctx = {
                .metric_name = metric_name,
                .operation = operation,
                .metric_regexp = metric_regexp,
                .metric_pattern = metric_pattern,
                .carg = carg,
                .an = an
            };
            alligator_ht_foreach_arg(labels, metric_transform_apply_operation, &ctx);
        }
    }
}

char* metric_transform_label_value(char *metric_name, char *metric_name_alt, char *label_name, char *label_value, json_t *metricstransform, context_arg *carg, action_node *an)
{
    if (!metric_name || !label_name || !label_value || !metricstransform)
        return NULL;

    alligator_ht *labels = alligator_ht_init(NULL);
    if (!labels)
        return NULL;

    labels_hash_insert_nocache(labels, label_name, label_value);
    metric_transform_labels(metric_name, metric_name_alt, labels, metricstransform, carg, an);

    /* Must use the same hash as labels_hash_insert_nocache (metrictree_hashfunc_get). */
    uint32_t label_hash = ac && ac->metrictree_hashfunc_get
        ? ac->metrictree_hashfunc_get(label_name)
        : tommy_strhash_u32(0, label_name);
    labels_container *labelscont = alligator_ht_search(
        labels, labels_hash_compare, label_name, label_hash
    );
    char *ret = NULL;
    if (labelscont && labelscont->key)
        ret = strdup(labelscont->key);

    labels_hash_free(labels);
    return ret;
}
