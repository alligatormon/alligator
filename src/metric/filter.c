#include "metric/filter.h"
#include "metric/namespace.h"
#include "metric/labels.h"
#include "metric/metrictree.h"
#include "events/context_arg.h"
#include "common/logs.h"
#include "main.h"
#include <pcre.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern aconf *ac;

typedef struct metric_filter_rule
{
	char *metric;
	int match_regexp;
	pcre *re;
} metric_filter_rule;

typedef struct metric_filter_label_rule
{
	char *label;
	char *value; /* NULL = any value for this label name */
	int match_regexp;
	pcre *re;
} metric_filter_label_rule;

struct metric_filter
{
	metric_filter_rule *include;
	size_t include_n;
	metric_filter_rule *exclude;
	size_t exclude_n;
	metric_filter_label_rule *include_label;
	size_t include_label_n;
	metric_filter_label_rule *exclude_label;
	size_t exclude_label_n;
	json_t *json;
};

static int metric_filter_match_type_is_regexp(const char *match_type)
{
	return match_type &&
		(!strcasecmp(match_type, "regexp") || !strcasecmp(match_type, "regex"));
}

static int metric_filter_match_type_is_strict(const char *match_type)
{
	return !match_type || !*match_type || !strcasecmp(match_type, "strict");
}

static const char *metric_filter_rule_pattern(json_t *rule)
{
	const char *metric;

	if (!rule || !json_is_object(rule))
		return NULL;

	metric = json_string_value(json_object_get(rule, "metric"));
	if (metric)
		return metric;
	/* Aliases for the pattern key */
	metric = json_string_value(json_object_get(rule, "name"));
	if (metric)
		return metric;
	metric = json_string_value(json_object_get(rule, "pattern"));
	if (metric)
		return metric;
	metric = json_string_value(json_object_get(rule, "include"));
	if (metric)
		return metric;
	metric = json_string_value(json_object_get(rule, "exclude"));
	return metric;
}

static void metric_filter_rule_clear(metric_filter_rule *r)
{
	if (!r)
		return;
	free(r->metric);
	r->metric = NULL;
	if (r->re)
	{
		pcre_free(r->re);
		r->re = NULL;
	}
}

static void metric_filter_label_rule_clear(metric_filter_label_rule *r)
{
	if (!r)
		return;
	free(r->label);
	r->label = NULL;
	free(r->value);
	r->value = NULL;
	if (r->re)
	{
		pcre_free(r->re);
		r->re = NULL;
	}
}

static int metric_filter_rule_init(metric_filter_rule *r, const char *metric, const char *match_type)
{
	const char *error = NULL;
	int erroffset = 0;

	memset(r, 0, sizeof(*r));
	if (!metric || !*metric)
		return 0;

	r->metric = strdup(metric);
	if (!r->metric)
		return 0;

	if (!metric_filter_match_type_is_strict(match_type) &&
	    !metric_filter_match_type_is_regexp(match_type))
	{
		glog(L_WARN, "metric_filter: unknown match_type '%s' for '%s', treating as strict\n",
			match_type, metric);
	}

	r->match_regexp = metric_filter_match_type_is_regexp(match_type);
	if (!r->match_regexp)
		return 1;

	r->re = pcre_compile(metric, 0, &error, &erroffset, NULL);
	if (!r->re)
	{
		glog(L_WARN, "metric_filter: pcre_compile failed for '%s' at offset %d: %s (rule skipped)\n",
			metric, erroffset, error ? error : "unknown");
		free(r->metric);
		r->metric = NULL;
		r->match_regexp = 0;
		return 0;
	}
	return 1;
}

static int metric_filter_label_rule_init(metric_filter_label_rule *r, const char *label,
	const char *value, const char *match_type)
{
	const char *error = NULL;
	int erroffset = 0;

	memset(r, 0, sizeof(*r));
	if (!label || !*label)
		return 0;

	r->label = strdup(label);
	if (!r->label)
		return 0;

	if (value && *value)
	{
		r->value = strdup(value);
		if (!r->value)
		{
			free(r->label);
			r->label = NULL;
			return 0;
		}
	}

	if (!metric_filter_match_type_is_strict(match_type) &&
	    !metric_filter_match_type_is_regexp(match_type))
	{
		glog(L_WARN, "metric_filter: unknown match_type '%s' for label '%s', treating as strict\n",
			match_type, label);
	}

	r->match_regexp = metric_filter_match_type_is_regexp(match_type);
	if (!r->match_regexp || !r->value)
		return 1;

	r->re = pcre_compile(r->value, 0, &error, &erroffset, NULL);
	if (!r->re)
	{
		glog(L_WARN, "metric_filter: pcre_compile failed for label '%s' value '%s' at offset %d: %s (rule skipped)\n",
			label, r->value, erroffset, error ? error : "unknown");
		metric_filter_label_rule_clear(r);
		return 0;
	}
	return 1;
}

static size_t metric_filter_load_rules(json_t *arr, metric_filter_rule **out)
{
	size_t n;
	size_t i;
	size_t filled = 0;
	metric_filter_rule *rules;

	*out = NULL;
	if (!arr || !json_is_array(arr))
		return 0;

	n = json_array_size(arr);
	if (!n)
		return 0;

	rules = calloc(n, sizeof(*rules));
	if (!rules)
		return 0;

	for (i = 0; i < n; ++i)
	{
		json_t *rule = json_array_get(arr, i);
		const char *metric = NULL;
		const char *match_type = "strict";

		if (json_is_string(rule))
		{
			metric = json_string_value(rule);
		}
		else if (json_is_object(rule))
		{
			metric = metric_filter_rule_pattern(rule);
			match_type = json_string_value(json_object_get(rule, "match_type"));
			if (!match_type)
				match_type = "strict";
		}

		if (!metric_filter_rule_init(&rules[filled], metric, match_type))
			continue;
		++filled;
	}

	if (!filled)
	{
		free(rules);
		return 0;
	}

	*out = rules;
	return filled;
}

static size_t metric_filter_load_label_rules(json_t *arr, metric_filter_label_rule **out)
{
	size_t n;
	size_t i;
	size_t filled = 0;
	metric_filter_label_rule *rules;

	*out = NULL;
	if (!arr || !json_is_array(arr))
		return 0;

	n = json_array_size(arr);
	if (!n)
		return 0;

	rules = calloc(n, sizeof(*rules));
	if (!rules)
		return 0;

	for (i = 0; i < n; ++i)
	{
		json_t *rule = json_array_get(arr, i);
		const char *label = NULL;
		const char *value = NULL;
		const char *match_type = "strict";

		if (!json_is_object(rule))
			continue;

		label = json_string_value(json_object_get(rule, "label"));
		if (!label)
			label = json_string_value(json_object_get(rule, "name"));
		value = json_string_value(json_object_get(rule, "value"));
		match_type = json_string_value(json_object_get(rule, "match_type"));
		if (!match_type)
			match_type = "strict";

		if (!metric_filter_label_rule_init(&rules[filled], label, value, match_type))
			continue;
		++filled;
	}

	if (!filled)
	{
		free(rules);
		return 0;
	}

	*out = rules;
	return filled;
}

metric_filter *metric_filter_compile(json_t *root)
{
	metric_filter *mf;
	json_t *include;
	json_t *exclude;
	json_t *include_label;
	json_t *exclude_label;

	if (!root || !json_is_object(root))
		return NULL;

	include = json_object_get(root, "include");
	exclude = json_object_get(root, "exclude");
	include_label = json_object_get(root, "include_label");
	exclude_label = json_object_get(root, "exclude_label");

	mf = calloc(1, sizeof(*mf));
	if (!mf)
		return NULL;

	mf->include_n = metric_filter_load_rules(include, &mf->include);
	mf->exclude_n = metric_filter_load_rules(exclude, &mf->exclude);
	mf->include_label_n = metric_filter_load_label_rules(include_label, &mf->include_label);
	mf->exclude_label_n = metric_filter_load_label_rules(exclude_label, &mf->exclude_label);
	mf->json = json_incref(root);

	if (!mf->include_n && !mf->exclude_n && !mf->include_label_n && !mf->exclude_label_n)
	{
		metric_filter_free(mf);
		return NULL;
	}

	return mf;
}

metric_filter *metric_filter_dup(const metric_filter *src)
{
	if (!src || !src->json)
		return NULL;
	return metric_filter_compile(src->json);
}

void metric_filter_free(metric_filter *mf)
{
	size_t i;

	if (!mf)
		return;

	for (i = 0; i < mf->include_n; ++i)
		metric_filter_rule_clear(&mf->include[i]);
	free(mf->include);

	for (i = 0; i < mf->exclude_n; ++i)
		metric_filter_rule_clear(&mf->exclude[i]);
	free(mf->exclude);

	for (i = 0; i < mf->include_label_n; ++i)
		metric_filter_label_rule_clear(&mf->include_label[i]);
	free(mf->include_label);

	for (i = 0; i < mf->exclude_label_n; ++i)
		metric_filter_label_rule_clear(&mf->exclude_label[i]);
	free(mf->exclude_label);

	if (mf->json)
		json_decref(mf->json);

	free(mf);
}

json_t *metric_filter_json(const metric_filter *mf)
{
	return mf ? mf->json : NULL;
}

static int metric_filter_rule_matches(const metric_filter_rule *r, const char *name)
{
	if (!r || !r->metric)
		return 0;

	if (!r->match_regexp)
		return name && !strcmp(name, r->metric);

	if (r->re)
	{
		int ovector[30];
		const char *v = name ? name : "";
		return pcre_exec(r->re, NULL, v, (int)strlen(v), 0, 0, ovector, 30) >= 0;
	}

	return metric_transform_match_pattern(name, r->metric, 1);
}

static int metric_filter_value_matches(const metric_filter_label_rule *r, const char *value)
{
	if (!r)
		return 0;

	/* No value on rule → any value matches. */
	if (!r->value)
		return 1;

	if (!r->match_regexp)
		return value && !strcmp(value, r->value);

	if (r->re)
	{
		int ovector[30];
		const char *v = value ? value : "";
		return pcre_exec(r->re, NULL, v, (int)strlen(v), 0, 0, ovector, 30) >= 0;
	}

	return metric_transform_match_pattern(value, r->value, 1);
}

static labels_container *metric_filter_labels_lookup(alligator_ht *labels, const char *name)
{
	uint32_t name_hash;

	if (!labels || !name || !*name)
		return NULL;

	if (ac && ac->metrictree_hashfunc_get)
		name_hash = (uint32_t)ac->metrictree_hashfunc_get(name);
	else
		name_hash = tommy_strhash_u32(0, name);

	return alligator_ht_search(labels, labels_hash_compare, name, name_hash);
}

static int metric_filter_label_rule_matches_sample(const metric_filter_label_rule *r, alligator_ht *labels)
{
	labels_container *lc;

	if (!r || !r->label)
		return 0;

	lc = metric_filter_labels_lookup(labels, r->label);
	if (!lc)
		return 0;

	return metric_filter_value_matches(r, lc->key);
}

static metric_filter *metric_filter_resolve(context_arg *carg)
{
	if (!carg)
		return NULL;

	if (carg->metric_filter_rules)
		return carg->metric_filter_rules;

	if (carg->srv_carg)
	{
		context_arg *srv = (context_arg *)carg->srv_carg;
		if (srv && srv->metric_filter_rules)
			return srv->metric_filter_rules;
	}

	return NULL;
}

int metric_filter_allow(context_arg *carg, const char *name, alligator_ht *labels)
{
	metric_filter *mf = metric_filter_resolve(carg);
	size_t i;
	int included;

	if (!mf)
		return 1;

	/* 1) name include */
	if (mf->include_n)
	{
		included = 0;
		for (i = 0; i < mf->include_n; ++i)
		{
			if (metric_filter_rule_matches(&mf->include[i], name))
			{
				included = 1;
				break;
			}
		}
		if (!included)
		{
			if (carg)
				carglog(carg, L_TRACE, "metric_filter: drop '%s' (no include match)\n",
					name ? name : "");
			return 0;
		}
	}

	/* 2) label include */
	if (mf->include_label_n)
	{
		included = 0;
		for (i = 0; i < mf->include_label_n; ++i)
		{
			if (metric_filter_label_rule_matches_sample(&mf->include_label[i], labels))
			{
				included = 1;
				break;
			}
		}
		if (!included)
		{
			if (carg)
				carglog(carg, L_TRACE, "metric_filter: drop '%s' (no include_label match)\n",
					name ? name : "");
			return 0;
		}
	}

	/* 3) name exclude */
	for (i = 0; i < mf->exclude_n; ++i)
	{
		if (metric_filter_rule_matches(&mf->exclude[i], name))
		{
			if (carg)
				carglog(carg, L_TRACE, "metric_filter: drop '%s' (exclude match)\n",
					name ? name : "");
			return 0;
		}
	}

	/* 4) label exclude */
	for (i = 0; i < mf->exclude_label_n; ++i)
	{
		if (metric_filter_label_rule_matches_sample(&mf->exclude_label[i], labels))
		{
			if (carg)
				carglog(carg, L_TRACE, "metric_filter: drop '%s' (exclude_label match)\n",
					name ? name : "");
			return 0;
		}
	}

	return 1;
}

void parse_metric_filter(context_arg *carg, json_t *root)
{
	json_t *json_metric_filter;
	json_t *parsed = NULL;

	if (!carg || !root)
		return;

	json_metric_filter = json_object_get(root, "metric_filter");
	if (!json_metric_filter)
		return;

	if (json_is_object(json_metric_filter))
		parsed = json_incref(json_metric_filter);
	else if (json_is_string(json_metric_filter))
	{
		const char *s = json_string_value(json_metric_filter);
		json_error_t error;
		json_t *obj = s ? json_loads(s, 0, &error) : NULL;
		if (obj && json_is_object(obj))
			parsed = obj;
		else
		{
			if (obj)
				json_decref(obj);
			carglog(carg, L_WARN, "context '%s': metric_filter string parse failed\n",
				carg->key ? carg->key : "?");
			return;
		}
	}
	else
	{
		carglog(carg, L_WARN, "context '%s': metric_filter has unsupported type=%d\n",
			carg->key ? carg->key : "?", json_typeof(json_metric_filter));
		return;
	}

	if (carg->metric_filter)
	{
		json_decref(carg->metric_filter);
		carg->metric_filter = NULL;
	}
	if (carg->metric_filter_rules)
	{
		metric_filter_free(carg->metric_filter_rules);
		carg->metric_filter_rules = NULL;
	}

	carg->metric_filter = parsed;
	carg->metric_filter_rules = metric_filter_compile(parsed);
	if (!carg->metric_filter_rules)
	{
		carglog(carg, L_WARN,
			"context '%s': metric_filter has no valid rules, clearing\n",
			carg->key ? carg->key : "?");
		json_decref(carg->metric_filter);
		carg->metric_filter = NULL;
	}
}
