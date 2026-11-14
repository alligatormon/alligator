#include "bpf/bpf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const bpf_plain_tok *at(const bpf_plain_tok *toks, uint64_t n, uint64_t i)
{
	if (i >= n)
		return NULL;
	return &toks[i];
}

static int is_brace_open(const bpf_plain_tok *t)
{
	return t && (t->start || (t->s && !strcmp(t->s, "{")));
}

static int is_brace_close(const bpf_plain_tok *t)
{
	return t && (t->end || (t->s && !strcmp(t->s, "}")));
}

static int split_kv(const char *s, char *key, size_t key_n, char *val, size_t val_n)
{
	const char *eq;
	size_t kn, vn;

	if (!s)
		return 0;
	eq = strchr(s, '=');
	if (!eq || eq == s)
		return 0;
	kn = (size_t)(eq - s);
	if (kn >= key_n)
		kn = key_n - 1;
	memcpy(key, s, kn);
	key[kn] = 0;
	vn = strlen(eq + 1);
	if (vn >= val_n)
		vn = val_n - 1;
	memcpy(val, eq + 1, vn);
	val[vn] = 0;
	return 1;
}

static void add_prefix(json_t *probe, const char *s)
{
	json_t *arr;

	if (!s || !s[0])
		return;
	arr = json_object_get(probe, "prefixes");
	if (!arr) {
		arr = json_array();
		json_object_set_new(probe, "prefixes", arr);
	}
	json_array_append_new(arr, json_string(s));
}

static void add_named_list(json_t *probe, const char *key, const char *s)
{
	json_t *arr;

	if (!s || !s[0])
		return;
	arr = json_object_get(probe, key);
	if (!arr) {
		arr = json_array();
		json_object_set_new(probe, key, arr);
	}
	json_array_append_new(arr, json_string(s));
}

static json_t *parse_statement(const bpf_plain_tok *toks, uint64_t n, uint64_t *i, int *depth)
{
	json_t *stmt = json_array();
	uint64_t k = *i;

	while (k < n) {
		const bpf_plain_tok *t = at(toks, n, k);
		if (is_brace_open(t)) {
			(*depth)++;
			k++;
			continue;
		}
		if (is_brace_close(t)) {
			(*depth)--;
			k++;
			break;
		}
		if (t->s && t->s[0])
			json_array_append_new(stmt, json_string(t->s));
		k++;
		if (t->semicolon)
			break;
	}
	*i = k;
	return stmt;
}

static void apply_arg_word(json_t *arg, const char *word)
{
	char key[32], val[BPF_STR_MAX];

	if (!split_kv(word, key, sizeof(key), val, sizeof(val)))
		return;
	if (!strcmp(key, "allow")) {
		json_t *arr = json_object_get(arg, "allow");
		if (!arr) {
			arr = json_array();
			json_object_set_new(arg, "allow", arr);
		}
		json_array_append_new(arr, json_string(val));
		return;
	}
	json_object_set_new(arg, key, json_string(val));
}

static void statement_to_probe(json_t *probe, json_t *stmt)
{
	size_t n, i;
	const char *w0;
	json_t *item;

	n = json_array_size(stmt);
	if (!n)
		return;
	w0 = json_string_value(json_array_get(stmt, 0));
	if (!w0)
		return;
	if (!strcmp(w0, "arg") && n >= 2) {
		json_t *args = json_object_get(probe, "args");
		json_t *arg = json_object();
		long idx = strtol(json_string_value(json_array_get(stmt, 1)), NULL, 10);
		if (!args) {
			args = json_array();
			json_object_set_new(probe, "args", args);
		}
		json_object_set_new(arg, "index", json_integer(idx));
		for (i = 2; i < n; i++)
			apply_arg_word(arg, json_string_value(json_array_get(stmt, i)));
		json_array_append_new(args, arg);
		return;
	}
	if (!strcmp(w0, "prefix") && n >= 2) {
		add_prefix(probe, json_string_value(json_array_get(stmt, 1)));
		return;
	}
	if (!strcmp(w0, "syscall") && n >= 2) {
		add_named_list(probe, "syscalls", json_string_value(json_array_get(stmt, 1)));
		return;
	}
	if (!strcmp(w0, "comm") && n >= 2) {
		add_named_list(probe, "comms", json_string_value(json_array_get(stmt, 1)));
		return;
	}
	if (!strcmp(w0, "binary") && n >= 2) {
		json_object_set_new(probe, "binary", json_string(json_string_value(json_array_get(stmt, 1))));
		return;
	}
	if (!strcmp(w0, "label") && n >= 2) {
		const char *lab = json_string_value(json_array_get(stmt, 1));
		if (lab && !strcmp(lab, "dport"))
			json_object_set_new(probe, "label_dport", json_true());
		return;
	}
	if (!strcmp(w0, "latency")) {
		json_object_set_new(probe, "latency", json_true());
		return;
	}
	if (!strcmp(w0, "mode") && n >= 2) {
		const char *m = json_string_value(json_array_get(stmt, 1));
		if (m && !strcmp(m, "capture"))
			json_object_set_new(probe, "mode", json_string("capture"));
		else
			json_object_set_new(probe, "mode", json_string("count"));
		return;
	}
	if (!strcmp(w0, "metric") && n >= 2) {
		const char *m = json_string_value(json_array_get(stmt, 1));
		if (m && m[0])
			json_object_set_new(probe, "metric", json_string(m));
		return;
	}
	if (!strcmp(w0, "pair_id") && n >= 2) {
		const char *m = json_string_value(json_array_get(stmt, 1));
		if (m && m[0])
			json_object_set_new(probe, "pair_id", json_string(m));
		return;
	}
	if (!strcmp(w0, "save") && n >= 2) {
		const char *a = json_string_value(json_array_get(stmt, 1));
		json_t *saves;
		long idx = -1;
		if (a && !strncmp(a, "arg", 3))
			idx = strtol(a + 3, NULL, 10);
		else if (a)
			idx = strtol(a, NULL, 10);
		if (idx < 0 || idx >= BPF_ARG_MAX)
			return;
		saves = json_object_get(probe, "saves");
		if (!saves) {
			saves = json_array();
			json_object_set_new(probe, "saves", saves);
		}
		json_array_append_new(saves, json_integer(idx));
		return;
	}
	if (!strcmp(w0, "when") && n >= 2) {
		const char *op = json_string_value(json_array_get(stmt, 1));
		long long v = 0;
		int ok = 0;
		if (op && n >= 3 && !strcmp(op, "retval_gt")) {
			const char *num = json_string_value(json_array_get(stmt, 2));
			if (num) {
				v = strtoll(num, NULL, 10);
				ok = 1;
			}
		} else if (op && !strncmp(op, "retval_gt=", 10)) {
			v = strtoll(op + 10, NULL, 10);
			ok = 1;
		}
		if (!ok)
			return;
		json_object_set_new(probe, "when", json_string("retval_gt"));
		json_object_set_new(probe, "when_value", json_integer(v));
		return;
	}
	if (!strcmp(w0, "regex")) {
		char on[32];
		char pat[BPF_REGEX_MAX];
		on[0] = 0;
		pat[0] = 0;
		for (i = 1; i < n; i++) {
			char key[32], val[BPF_REGEX_MAX];
			const char *w = json_string_value(json_array_get(stmt, i));
			/*
			 * pattern="..." is one quoted token. The plain tokenizer keeps only
			 * the quoted text, so the word arrives without the pattern= prefix.
			 * on=LABEL is unquoted and still has the key.
			 */
			if (!w || !w[0])
				continue;
			if (split_kv(w, key, sizeof(key), val, sizeof(val)) &&
			    (!strcmp(key, "on") || !strcmp(key, "pattern"))) {
				if (!strcmp(key, "on"))
					snprintf(on, sizeof(on), "%s", val);
				else
					snprintf(pat, sizeof(pat), "%s", val);
			} else
				snprintf(pat, sizeof(pat), "%s", w);
		}
		if (on[0])
			json_object_set_new(probe, "regex_on", json_string(on));
		if (pat[0])
			json_object_set_new(probe, "regex_pattern", json_string(pat));
		return;
	}
	item = json_array_get(stmt, 0);
	if (item && json_is_string(item) && strchr(json_string_value(item), '=')) {
		char key[32], val[BPF_STR_MAX];
		if (split_kv(json_string_value(item), key, sizeof(key), val, sizeof(val))) {
			if (!strcmp(key, "filter"))
				json_object_set_new(probe, "filter", json_string(val));
			else if (!strcmp(key, "value"))
				json_object_set_new(probe, "filter_value", json_string(val));
			else if (!strcmp(key, "binary"))
				json_object_set_new(probe, "binary", json_string(val));
		}
		for (i = 1; i < n; i++) {
			const char *w = json_string_value(json_array_get(stmt, i));
			char key2[32], val2[BPF_STR_MAX];
			if (w && split_kv(w, key2, sizeof(key2), val2, sizeof(val2))) {
				if (!strcmp(key2, "value"))
					json_object_set_new(probe, "filter_value", json_string(val2));
				else if (!strcmp(key2, "filter"))
					json_object_set_new(probe, "filter", json_string(val2));
			}
		}
	}
	(void)item;
}

static json_t *finish_probe(const char *kind, const char *symbol)
{
	json_t *probe = json_object();

	json_object_set_new(probe, "kind", json_string(kind));
	if (symbol && symbol[0]) {
		if (!strcmp(kind, "probe"))
			json_object_set_new(probe, "name", json_string(symbol));
		else
			json_object_set_new(probe, "symbol", json_string(symbol));
	}
	return probe;
}

json_t *bpf_plain_parse(const bpf_plain_tok *toks, uint64_t n, uint64_t *idx)
{
	json_t *root = json_object();
	json_t *probes = json_array();
	uint64_t i;
	int depth = 0;
	json_t *cur = NULL;

	json_object_set_new(root, "probes", probes);
	if (!toks || !idx)
		return root;
	i = *idx;
	while (i < n) {
		const bpf_plain_tok *t = at(toks, n, i);
		if (!t)
			break;
		if (is_brace_open(t)) {
			depth++;
			i++;
			continue;
		}
		if (is_brace_close(t)) {
			depth--;
			i++;
			if (depth <= 0) {
				if (cur) {
					json_array_append_new(probes, cur);
					cur = NULL;
				}
				break;
			}
			if (cur && depth == 1) {
				json_array_append_new(probes, cur);
				cur = NULL;
			}
			continue;
		}
		if (depth == 1 && t->s && t->s[0]) {
			char key[32], val[BPF_STR_MAX];

			if (!strcmp(t->s, "cgroup_labels")) {
				json_object_set_new(root, "cgroup_labels", json_true());
				i++;
				continue;
			}
			if (!strcmp(t->s, "namespace")) {
				const bpf_plain_tok *name = at(toks, n, i + 1);
				if (name && name->s && name->s[0] && !is_brace_open(name) && !is_brace_close(name)) {
					json_object_set_new(root, "namespace", json_string(name->s));
					i += 2;
					continue;
				}
			}
			if (split_kv(t->s, key, sizeof(key), val, sizeof(val)) && !strcmp(key, "namespace") && val[0]) {
				json_object_set_new(root, "namespace", json_string(val));
				i++;
				continue;
			}
			if (!strcmp(t->s, "kprobe") || !strcmp(t->s, "uprobe") || !strcmp(t->s, "uretprobe") || !strcmp(t->s, "probe")) {
				const char *kind = t->s;
				const bpf_plain_tok *sym;
				const char *symbol = "";
				i++;
				sym = at(toks, n, i);
				if (sym && sym->s && sym->s[0] && !is_brace_open(sym) && !is_brace_close(sym)) {
					symbol = sym->s;
					i++;
				}
				if (cur) {
					json_array_append_new(probes, cur);
					cur = NULL;
				}
				cur = finish_probe(kind, symbol);
				if (sym && sym->semicolon) {
					json_array_append_new(probes, cur);
					cur = NULL;
				}
				continue;
			}
		}
		if (cur && depth >= 1) {
			json_t *stmt = parse_statement(toks, n, &i, &depth);
			statement_to_probe(cur, stmt);
			json_decref(stmt);
			if (depth <= 0)
				break;
			continue;
		}
		i++;
	}
	if (cur)
		json_array_append_new(probes, cur);
	*idx = i ? i - 1 : 0;
	return root;
}
