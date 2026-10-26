#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "common/http_limit.h"
#include "common/logs.h"
#include "events/context_arg.h"
#include "parsers/http_proto.h"

#define HTTP_LIMIT_KEY_MAX 8
#define HTTP_LIMIT_RULES_MAX 32
#define HTTP_LIMIT_SLOTS 4096
#define HTTP_LIMIT_VALUE_MAX 4096
#define HTTP_LIMIT_PATH_MAX 512
#define HTTP_LIMIT_RPS_MAX 1000000000u
#define HTTP_LIMIT_IDLE_NS (60ULL * 1000000000ULL)
#define HTTP_LIMIT_TABLE_RETRY_SEC 60

enum {
	HTTP_LIMIT_KEY_REMOTE_ADDR = 1,
	HTTP_LIMIT_KEY_HEADER = 2,
	HTTP_LIMIT_KEY_COOKIE = 3
};

typedef struct http_limit_key {
	uint8_t kind;
	char *name;
} http_limit_key;

typedef struct http_limit_slot {
	char *id;
	uint64_t tat;
	uint64_t last;
	uint8_t have;
} http_limit_slot;

typedef struct http_limit_rule {
	char *path;
	uint32_t rps;
	uint64_t interval_ns;
	uint64_t tau_ns;
	http_limit_key keys[HTTP_LIMIT_KEY_MAX];
	uint8_t nkeys;
	pthread_mutex_t lock;
	uint8_t lock_ready;
	uint64_t tat;
	uint8_t have;
	http_limit_slot *slots;
	uint32_t nslots;
} http_limit_rule;

struct http_limit {
	http_limit_rule *rules;
	uint32_t nrules;
	uint32_t ref;
};

typedef struct http_limit_plan {
	int deny;
	uint64_t retry;
	uint64_t new_tat;
	int slot;
	int insert;
	uint32_t reuse_at;
	char *id;
} http_limit_plan;

static int http_limit_name_ok(int kind, const char *name)
{
	const char *p;

	if (!name || !name[0] || strlen(name) > 128)
		return 0;
	for (p = name; *p; p++)
	{
		unsigned char c = (unsigned char)*p;

		if (c <= 32 || c >= 127)
			return 0;
		if (kind == HTTP_LIMIT_KEY_COOKIE && (c == ';' || c == '=' || c == ','))
			return 0;
		if (kind == HTTP_LIMIT_KEY_HEADER && c == ':')
			return 0;
	}
	return 1;
}

static void http_limit_rule_clear(http_limit_rule *rule)
{
	uint8_t i;

	if (!rule)
		return;
	free(rule->path);
	for (i = 0; i < rule->nkeys; i++)
		free(rule->keys[i].name);
	if (rule->slots)
	{
		uint32_t s;

		for (s = 0; s < rule->nslots; s++)
			free(rule->slots[s].id);
		free(rule->slots);
	}
	if (rule->lock_ready)
		pthread_mutex_destroy(&rule->lock);
	memset(rule, 0, sizeof(*rule));
}

static void http_limit_destroy(struct http_limit *lim)
{
	uint32_t i;

	if (!lim)
		return;
	for (i = 0; i < lim->nrules; i++)
		http_limit_rule_clear(&lim->rules[i]);
	free(lim->rules);
	free(lim);
}

void http_limit_incref(struct http_limit *lim)
{
	if (lim)
		__sync_add_and_fetch(&lim->ref, 1);
}

void http_limit_decref(struct http_limit *lim)
{
	if (!lim)
		return;
	if (__sync_sub_and_fetch(&lim->ref, 1) == 0)
		http_limit_destroy(lim);
}

static int http_limit_path_ok(const char *path)
{
	size_t n;

	if (!path || path[0] != '/')
		return 0;
	n = strlen(path);
	if (!n || n > HTTP_LIMIT_PATH_MAX)
		return 0;
	if (strchr(path, '?') || strchr(path, ' ') || strchr(path, '\t') || strchr(path, '\r') || strchr(path, '\n'))
		return 0;
	return 1;
}

static int http_limit_parse_key(const char *spec, http_limit_key *key)
{
	const char *name;

	if (!spec || !key)
		return 0;
	memset(key, 0, sizeof(*key));
	if (!strcmp(spec, "remote_addr"))
	{
		key->kind = HTTP_LIMIT_KEY_REMOTE_ADDR;
		return 1;
	}
	if (!strncmp(spec, "header:", 7))
	{
		name = spec + 7;
		if (!http_limit_name_ok(HTTP_LIMIT_KEY_HEADER, name))
			return 0;
		key->name = strdup(name);
		if (!key->name)
			return 0;
		key->kind = HTTP_LIMIT_KEY_HEADER;
		return 1;
	}
	if (!strncmp(spec, "cookie:", 7))
	{
		name = spec + 7;
		if (!http_limit_name_ok(HTTP_LIMIT_KEY_COOKIE, name))
			return 0;
		key->name = strdup(name);
		if (!key->name)
			return 0;
		key->kind = HTTP_LIMIT_KEY_COOKIE;
		return 1;
	}
	return 0;
}

static int http_limit_rule_fill(http_limit_rule *rule, json_t *obj)
{
	json_t *jpath;
	json_t *jrps;
	json_t *jkeys;
	const char *path;
	json_int_t rps;
	size_t nkeys;
	size_t i;

	if (!rule || !json_is_object(obj))
		return 0;
	jpath = json_object_get(obj, "path");
	jrps = json_object_get(obj, "rps");
	path = json_is_string(jpath) ? json_string_value(jpath) : NULL;
	if (!http_limit_path_ok(path) || !json_is_integer(jrps))
		return 0;
	rps = json_integer_value(jrps);
	if (rps < 1 || (uint64_t)rps > HTTP_LIMIT_RPS_MAX)
		return 0;

	rule->path = strdup(path);
	if (!rule->path)
		return 0;
	rule->rps = (uint32_t)rps;
	rule->interval_ns = 1000000000ULL / (uint64_t)rule->rps;
	rule->tau_ns = rule->interval_ns * (uint64_t)(rule->rps - 1);
	if (!rule->interval_ns)
	{
		http_limit_rule_clear(rule);
		return 0;
	}

	jkeys = json_object_get(obj, "key");
	if (jkeys)
	{
		if (!json_is_array(jkeys))
		{
			http_limit_rule_clear(rule);
			return 0;
		}
		nkeys = json_array_size(jkeys);
		if (nkeys > HTTP_LIMIT_KEY_MAX)
		{
			http_limit_rule_clear(rule);
			return 0;
		}
		for (i = 0; i < nkeys; i++)
		{
			const char *spec = json_string_value(json_array_get(jkeys, i));

			if (!http_limit_parse_key(spec, &rule->keys[rule->nkeys]))
			{
				http_limit_rule_clear(rule);
				return 0;
			}
			rule->nkeys++;
		}
	}

	if (rule->nkeys)
	{
		rule->slots = calloc(HTTP_LIMIT_SLOTS, sizeof(*rule->slots));
		if (!rule->slots)
		{
			http_limit_rule_clear(rule);
			return 0;
		}
	}
	if (pthread_mutex_init(&rule->lock, NULL))
	{
		http_limit_rule_clear(rule);
		return 0;
	}
	rule->lock_ready = 1;
	return 1;
}

int http_limit_apply_json(struct context_arg *carg, json_t *jlimit)
{
	struct http_limit *lim;
	http_limit_rule *rules;
	uint32_t n = 0;
	size_t i;
	size_t njson;

	if (!carg)
		return -1;
	if (!jlimit)
		return 0;
	if (!json_is_array(jlimit))
	{
		glog(L_ERROR, "entrypoint: limit must be an array\n");
		carg->http_limit_config_error = 1;
		return -1;
	}

	njson = json_array_size(jlimit);
	rules = calloc(HTTP_LIMIT_RULES_MAX, sizeof(*rules));
	if (!rules)
	{
		if (njson > 0)
			carg->http_limit_config_error = 1;
		return 0;
	}

	{
		int rejected = 0;

		for (i = 0; i < njson; i++)
		{
			if (n >= HTTP_LIMIT_RULES_MAX)
			{
				glog(L_ERROR, "entrypoint: limit block rejected\n");
				rejected = 1;
				break;
			}
			if (!http_limit_rule_fill(&rules[n], json_array_get(jlimit, i)))
			{
				glog(L_ERROR, "entrypoint: limit block rejected\n");
				http_limit_rule_clear(&rules[n]);
				rejected = 1;
				continue;
			}
			n++;
		}

		/* A configured limit that did not install is not the same as no limit. */
		if (rejected || !n)
		{
			for (i = 0; i < n; i++)
				http_limit_rule_clear(&rules[i]);
			free(rules);
			if (rejected)
				carg->http_limit_config_error = 1;
			return 0;
		}
	}

	lim = calloc(1, sizeof(*lim));
	if (!lim)
	{
		for (i = 0; i < n; i++)
			http_limit_rule_clear(&rules[i]);
		free(rules);
		carg->http_limit_config_error = 1;
		return 0;
	}
	lim->rules = rules;
	lim->nrules = n;
	lim->ref = 1;
	if (carg->http_limit)
		http_limit_decref(carg->http_limit);
	carg->http_limit = lim;
	return (int)n;
}

static int http_limit_path_match(const char *rule_path, const char *uri)
{
	size_t n;
	const char *q;

	if (!rule_path || !uri)
		return 0;
	q = strchr(uri, '?');
	n = q ? (size_t)(q - uri) : strlen(uri);
	if (n != strlen(rule_path))
		return 0;
	return memcmp(uri, rule_path, n) == 0;
}

static int http_limit_id_add(char **buf, size_t *len, size_t *cap, const char *s, size_t n)
{
	char dec[32];
	int dlen;
	size_t need;
	char *nb;

	dlen = snprintf(dec, sizeof(dec), "%zu:", n);
	if (dlen <= 0 || (size_t)dlen >= sizeof(dec))
		return 0;
	need = *len + (size_t)dlen + n + 1;
	if (need > *cap)
	{
		size_t ncap = *cap ? *cap : 64;

		while (ncap < need)
		{
			if (ncap > ((size_t)-1) / 2)
				return 0;
			ncap *= 2;
		}
		nb = realloc(*buf, ncap);
		if (!nb)
			return 0;
		*buf = nb;
		*cap = ncap;
	}
	memcpy(*buf + *len, dec, (size_t)dlen);
	*len += (size_t)dlen;
	if (n && s)
		memcpy(*buf + *len, s, n);
	*len += n;
	(*buf)[*len] = '\0';
	return 1;
}

static int http_limit_key_label(const http_limit_key *key, char *out, size_t outlen)
{
	int n;

	if (!key || !out || !outlen)
		return -1;
	if (key->kind == HTTP_LIMIT_KEY_REMOTE_ADDR)
		n = snprintf(out, outlen, "remote_addr");
	else if (key->kind == HTTP_LIMIT_KEY_HEADER)
		n = snprintf(out, outlen, "header:%s", key->name ? key->name : "");
	else if (key->kind == HTTP_LIMIT_KEY_COOKIE)
		n = snprintf(out, outlen, "cookie:%s", key->name ? key->name : "");
	else
		return -1;
	if (n <= 0 || (size_t)n >= outlen)
		return -1;
	return n;
}

/* Missing, empty, duplicate, or overlong values share the empty bucket. */
static char *http_limit_value(context_arg *carg, http_reply_data *http, const http_limit_key *key)
{
	char *v = NULL;

	if (!key)
		return strdup("");
	if (key->kind == HTTP_LIMIT_KEY_REMOTE_ADDR)
	{
		if (!carg || !carg->authorize_peer[0])
			return strdup("");
		return strdup(carg->authorize_peer);
	}
	if (key->kind == HTTP_LIMIT_KEY_HEADER)
		v = http_header_get(http, key->name);
	else if (key->kind == HTTP_LIMIT_KEY_COOKIE)
		v = http_cookie_get(http, key->name);
	if (!v || !v[0] || strlen(v) > HTTP_LIMIT_VALUE_MAX)
	{
		free(v);
		return strdup("");
	}
	return v;
}

static int http_limit_build_id(context_arg *carg, http_reply_data *http, const http_limit_rule *rule, char **id_out)
{
	char *id = NULL;
	size_t len = 0;
	size_t cap = 0;
	uint8_t i;

	*id_out = NULL;
	if (!rule || !rule->nkeys)
		return 1;

	for (i = 0; i < rule->nkeys; i++)
	{
		char label[160];
		int llen;
		char *value;
		size_t vlen;

		llen = http_limit_key_label(&rule->keys[i], label, sizeof(label));
		value = http_limit_value(carg, http, &rule->keys[i]);
		if (llen < 0 || !value)
		{
			free(value);
			free(id);
			return 0;
		}
		vlen = strlen(value);
		if (!http_limit_id_add(&id, &len, &cap, label, (size_t)llen) ||
		    !http_limit_id_add(&id, &len, &cap, value, vlen))
		{
			free(value);
			free(id);
			return 0;
		}
		free(value);
	}
	*id_out = id;
	return 1;
}

static int http_limit_gcra_plan(uint64_t tat, int have, uint64_t now, uint64_t interval, uint64_t tau, uint64_t *new_tat, uint64_t *retry)
{
	uint64_t base;
	uint64_t wait;
	uint64_t sec;

	if (!have || tat < now)
		base = now;
	else
		base = tat;
	if (base - now > tau)
	{
		wait = base - now - tau;
		sec = (wait + 999999999ULL) / 1000000000ULL;
		if (sec < 1)
			sec = 1;
		if (retry)
			*retry = sec;
		return 0;
	}
	if (new_tat)
		*new_tat = base + interval;
	return 1;
}

static void http_limit_touch(http_limit_slot *slot, uint64_t now)
{
	if (slot && now >= slot->last)
		slot->last = now;
}

static int http_limit_rule_plan(http_limit_rule *rule, char *id, uint64_t now, http_limit_plan *plan)
{
	uint32_t i;

	plan->deny = 0;
	plan->retry = 1;
	plan->new_tat = 0;
	plan->slot = -1;
	plan->insert = 0;
	plan->reuse_at = 0;
	plan->id = id;

	if (!rule->nkeys)
	{
		if (!http_limit_gcra_plan(rule->tat, rule->have, now, rule->interval_ns, rule->tau_ns, &plan->new_tat, &plan->retry))
			plan->deny = 1;
		return 1;
	}
	if (!id)
	{
		plan->deny = 1;
		return 1;
	}

	for (i = 0; i < rule->nslots; i++)
	{
		if (rule->slots[i].id && id && !strcmp(rule->slots[i].id, id))
		{
			plan->slot = (int)i;
			http_limit_touch(&rule->slots[i], now);
			if (!http_limit_gcra_plan(rule->slots[i].tat, rule->slots[i].have, now, rule->interval_ns, rule->tau_ns, &plan->new_tat, &plan->retry))
				plan->deny = 1;
			return 1;
		}
	}

	if (rule->nslots < HTTP_LIMIT_SLOTS)
	{
		plan->insert = 1;
		if (!http_limit_gcra_plan(0, 0, now, rule->interval_ns, rule->tau_ns, &plan->new_tat, &plan->retry))
			plan->deny = 1;
		return 1;
	}

	for (i = 0; i < rule->nslots; i++)
	{
		http_limit_slot *slot = &rule->slots[i];

		if (slot->have && now >= slot->last && now - slot->last >= HTTP_LIMIT_IDLE_NS)
		{
			plan->insert = 2;
			plan->reuse_at = i;
			if (!http_limit_gcra_plan(0, 0, now, rule->interval_ns, rule->tau_ns, &plan->new_tat, &plan->retry))
				plan->deny = 1;
			return 1;
		}
	}

	plan->deny = 1;
	plan->retry = HTTP_LIMIT_TABLE_RETRY_SEC;
	return 1;
}

static void http_limit_rule_commit(http_limit_rule *rule, http_limit_plan *plan, uint64_t now)
{
	http_limit_slot *slot;

	if (!rule->nkeys)
	{
		rule->tat = plan->new_tat;
		rule->have = 1;
		free(plan->id);
		plan->id = NULL;
		return;
	}

	if (plan->slot >= 0)
	{
		slot = &rule->slots[plan->slot];
		slot->tat = plan->new_tat;
		slot->have = 1;
		http_limit_touch(slot, now);
		free(plan->id);
		plan->id = NULL;
		return;
	}

	if (plan->insert == 1)
	{
		slot = &rule->slots[rule->nslots++];
		slot->id = plan->id;
		plan->id = NULL;
		slot->tat = plan->new_tat;
		slot->have = 1;
		slot->last = now;
		return;
	}

	if (plan->insert == 2)
	{
		slot = &rule->slots[plan->reuse_at];
		free(slot->id);
		slot->id = plan->id;
		plan->id = NULL;
		slot->tat = plan->new_tat;
		slot->have = 1;
		slot->last = now;
	}
}

int http_limit_check(struct context_arg *carg, struct http_reply_data *http, uint64_t now_ns, uint64_t *retry_after_sec)
{
	struct http_limit *lim;
	uint32_t match[HTTP_LIMIT_RULES_MAX];
	http_limit_plan plans[HTTP_LIMIT_RULES_MAX];
	uint32_t nmatch = 0;
	uint32_t i;
	int deny = 0;
	uint64_t retry = 1;
	const char *uri;

	if (retry_after_sec)
		*retry_after_sec = 1;
	if (!carg || !carg->http_limit || !http)
		return 1;
	lim = carg->http_limit;
	uri = http->uri;
	if (!uri)
		return 1;

	for (i = 0; i < lim->nrules; i++)
	{
		if (http_limit_path_match(lim->rules[i].path, uri))
		{
			if (nmatch < HTTP_LIMIT_RULES_MAX)
				match[nmatch++] = i;
		}
	}
	if (!nmatch)
		return 1;

	for (i = 0; i < nmatch; i++)
	{
		plans[i].id = NULL;
		if (!http_limit_build_id(carg, http, &lim->rules[match[i]], &plans[i].id))
		{
			uint32_t j;

			for (j = 0; j < i; j++)
				free(plans[j].id);
			if (retry_after_sec)
				*retry_after_sec = 1;
			return 0;
		}
	}

	for (i = 0; i < nmatch; i++)
		pthread_mutex_lock(&lim->rules[match[i]].lock);

	for (i = 0; i < nmatch; i++)
	{
		http_limit_rule_plan(&lim->rules[match[i]], plans[i].id, now_ns, &plans[i]);
		if (plans[i].deny && !deny)
		{
			deny = 1;
			retry = plans[i].retry ? plans[i].retry : 1;
		}
	}

	if (!deny)
	{
		for (i = 0; i < nmatch; i++)
			http_limit_rule_commit(&lim->rules[match[i]], &plans[i], now_ns);
	}
	else
	{
		for (i = 0; i < nmatch; i++)
			free(plans[i].id);
	}

	for (i = nmatch; i-- > 0; )
		pthread_mutex_unlock(&lim->rules[match[i]].lock);

	if (deny && retry_after_sec)
		*retry_after_sec = retry;
	return deny ? 0 : 1;
}
