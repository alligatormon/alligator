#include "metric/namespace.h"
#include "action/type.h"
#include <ctype.h>

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

    free(new_name);
    return result;
}