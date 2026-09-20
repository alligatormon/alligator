#include "x509/type.h"
#include <string.h>
#include "main.h"
#include "lang/type.h"
#include "common/logs.h"
#include "scheduler/type.h"
#include "common/units.h"
#include "common/revocation.h"
#include "events/context_arg.h"

static string_tokens *x509_json_to_tokens(json_t *jtokens)
{
	string_tokens *tokens = NULL;

	if (!jtokens)
		return NULL;
	if (json_is_array(jtokens)) {
		uint64_t n = json_array_size(jtokens);
		for (uint64_t i = 0; i < n; i++) {
			if (!tokens)
				tokens = string_tokens_new();
			json_t *str = json_array_get(jtokens, i);
			string_tokens_push_dupn(tokens, (char *)json_string_value(str), json_string_length(str));
		}
	} else if (json_is_string(jtokens)) {
		tokens = string_tokens_new();
		string_tokens_push_dupn(tokens, (char *)json_string_value(jtokens), json_string_length(jtokens));
	}
	return tokens;
}

int tls_fs_push(char *name, char *path, string_tokens *tokens_match, string_tokens *tokens_except, char *password, char *ca_file, char *type, uint64_t period, json_t *x509) {
	glog(L_DEBUG, "run tls_fs_push with name %s, path %s, and password/passtr %p\n", name, path, password);
	x509_fs_t *tls_fs = calloc(1, sizeof(*tls_fs));
	tls_fs->name = strdup(name);
	tls_fs->path = strdup(path);
	tls_fs->match = tokens_match;
	tls_fs->except = tokens_except;
	tls_fs->period = period;

	if (password)
		tls_fs->password = strdup(password);
	if (ca_file)
		tls_fs->ca_file = strdup(ca_file);

	revocation_policy_init(&tls_fs->rev);
	if (x509)
		revocation_policy_parse_json(&tls_fs->rev, x509, 0);
	tls_fs->rev.fetch = REV_FETCH_INLINE;

	tls_fs->fctx.password = tls_fs->password;
	tls_fs->fctx.ca_file = tls_fs->ca_file;
	tls_fs->fctx.pol = &tls_fs->rev;
	tls_fs->carg = calloc(1, sizeof(context_arg));
	parse_add_label(tls_fs->carg, x509);
	parse_metricstransform(tls_fs->carg, x509);
	tls_fs->fctx.carg = tls_fs->carg;

	if (type && !strcmp(type, "pfx"))
		tls_fs->type = X509_TYPE_PFX;

	alligator_ht_insert(ac->fs_x509, &(tls_fs->node), tls_fs, tommy_strhash_u32(0, tls_fs->name));

	if (tls_fs->period) {
		tls_fs->period_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
		tls_fs->period_timer->data = tls_fs;
		uv_timer_init(ac->loop, tls_fs->period_timer);
		uv_timer_start(tls_fs->period_timer, for_tls_fs_recurse_repeat_period, tls_fs->period, tls_fs->period);
	}

	return 1;
}



int jks_push(char *name, char *path, string_tokens *tokens_match, string_tokens *tokens_except, char *password, char *passtr, uint64_t period, json_t *x509) {
	string *match = string_tokens_join(tokens_match, ",", 1);
	string *except = tokens_except && tokens_except->l ? string_tokens_join(tokens_except, ",", 1) : NULL;
	glog(L_DEBUG, "run jks_push with name %s, path %s, match %s, except %s, and password/passtr %p/%p\n",
		name, path, match->s, except ? except->s : "", password, passtr);

	if (!password && !passtr)
	{
		glog(L_INFO, "no set password for jks: %s\n", name);
		string_free(match);
		if (except)
			string_free(except);
		return 0;
	}

	lang_options *lo = calloc(1, sizeof(*lo));
	lo->key = strdup(name);
	lo->lang = strdup("so");
	lo->module = strdup("parseJks");
	lo->method = strdup("alligator_call");
	lo->hidden_arg = 1;
	lo->carg = calloc(1, sizeof(context_arg));
	lo->carg_allocated = 1;
	lo->carg->log_level = ac->system_carg->log_level;
	lo->carg->no_metric = 1;
	parse_add_label(lo->carg, x509);
	parse_metricstransform(lo->carg, x509);

	if (!passtr)
	{
		const char *except_s = except ? except->s : "";
		size_t len = strlen(path) + match->l + strlen(password) + strlen(except_s) + 4;
		passtr = malloc(len + 1);
		snprintf(passtr, len + 1, "%s %s %s %s", path, match->s, password, except_s);
	}

	lo->arg = passtr;

	lang_push_options(lo);
	scheduler_node* sn = scheduler_get(name);
	if (!sn) {
		sn = calloc(1, sizeof(*sn));
		sn->name = strdup(name);
		sn->period = period;
		sn->lang = strdup(name);
		uint32_t hash = tommy_strhash_u32(0, sn->name);
		alligator_ht_insert(ac->scheduler, &(sn->node), sn, hash);
		scheduler_start(sn);
	}

	const char *module_key = "parseJks";
	module_t *module = alligator_ht_search(ac->modules, module_compare, module_key, tommy_strhash_u32(0, module_key));
	if (!module)
	{
		module_t *module = calloc(1, sizeof(*module));
		module->key = strdup(module_key);
		module->path = strdup("/var/lib/alligator/parseJks.so");
		alligator_ht_insert(ac->modules, &(module->node), module, tommy_strhash_u32(0, module->key));
	}

	string_free(match);
	if (except)
		string_free(except);

	return 1;
}


/* Basename-only glob markers (same as aggregate filetailer). */
static int x509_path_has_glob(const char *s)
{
	if (!s || !*s)
		return 0;
	return strchr(s, '*') || strchr(s, '?') || strchr(s, '[');
}

/* If path basename contains a glob, truncate path to the directory and
 * optionally return the basename pattern (caller owns *pattern_out).
 * Returns 1 when a basename glob was found. */
static int x509_apply_path_basename_glob(char *path, char **pattern_out)
{
	char *slash;
	char *base;

	if (!path || !x509_path_has_glob(path))
		return 0;

	slash = strrchr(path, '/');
	if (!slash) {
		/* Pattern only — crawl cwd. */
		if (pattern_out && !*pattern_out)
			*pattern_out = strdup(path);
		path[0] = '.';
		path[1] = '\0';
		return 1;
	}

	base = slash + 1;
	if (pattern_out && !*pattern_out && *base)
		*pattern_out = strdup(base);
	*slash = '\0';
	/* Empty dir after truncate (e.g. slash-star.pem at root) → root. */
	if (!path[0]) {
		path[0] = '/';
		path[1] = '\0';
	}
	return 1;
}

int x509_push(json_t *x509) {
	json_t *jname = json_object_get(x509, "name");
	if (!jname) {
		glog(L_INFO, "not specified param 'name' in x509 context\n");
		return 0;
	}
	char *name = (char*)json_string_value(jname);

	json_t *jpath = json_object_get(x509, "path");
	if (!jpath) {
		glog(L_INFO, "not specified param 'path' in x509 context\n");
		return 0;
	}
	char *path_data = (char*)json_string_value(jpath);
	size_t path_len = json_string_length(jpath);
	char *path = strndup(path_data, path_len);
	while (path_len && path[path_len-1] == '/')
		--path_len;
	path[path_len] = 0;

	char *path_glob_pattern = NULL;
	x509_apply_path_basename_glob(path, &path_glob_pattern);

	string_tokens *tokens_match = x509_json_to_tokens(json_object_get(x509, "match"));
	string_tokens *tokens_except = x509_json_to_tokens(json_object_get(x509, "except"));

	/* Explicit match wins; path basename glob only seeds match when empty. */
	if ((!tokens_match || !tokens_match->l) && path_glob_pattern) {
		if (!tokens_match)
			tokens_match = string_tokens_new();
		string_tokens_push_dupn(tokens_match, path_glob_pattern, strlen(path_glob_pattern));
	}
	free(path_glob_pattern);

	if (!tokens_match || !tokens_match->l) {
		glog(L_INFO, "not specified param 'match' in x509 context (and path has no basename glob)\n");
		if (tokens_match)
			string_tokens_free(tokens_match);
		if (tokens_except)
			string_tokens_free(tokens_except);
		free(path);
		return 0;
	}

	json_t *jpassword = json_object_get(x509, "password");
	char *password = (char*)json_string_value(jpassword);

	json_t *jca_file = json_object_get(x509, "ca_file");
	char *ca_file = (char*)json_string_value(jca_file);

	json_t *jtype = json_object_get(x509, "type");
	char *type = (char*)json_string_value(jtype);

	uint64_t period = 10000;
	json_t *json_period = json_object_get(x509, "period");
	if (json_period)
		period = get_ms_from_human_range(json_string_value(json_period), json_string_length(json_period));

	if (type && !strcmp(type, "jks")) {
		int ret = jks_push(name, path, tokens_match, tokens_except, password, NULL, period, x509);
		free(path);
		if (tokens_match)
			string_tokens_free(tokens_match);
		if (tokens_except)
			string_tokens_free(tokens_except);
		return ret;
	}
	else
	{
		int ret = tls_fs_push(name, path, tokens_match, tokens_except, password, ca_file, type, period, x509);
		free(path);
		return ret;
	}
}
