#include "x509/type.h"
#include <string.h>
#include "main.h"
#include "lang/type.h"
#include "common/logs.h"
#include "scheduler/type.h"
#include "common/units.h"


int tls_fs_push(char *name, char *path, string_tokens *tokens_match, char *password, char *type, uint64_t period) {
	glog(L_DEBUG, "run tls_fs_push with name %s, path %s, and password/passtr %p\n", name, path, password);
	x509_fs_t *tls_fs = calloc(1, sizeof(*tls_fs));
	tls_fs->name = strdup(name);
	tls_fs->path = strdup(path);
	tls_fs->match = tokens_match;
	tls_fs->period = period;

	if (password)
		tls_fs->password = strdup(password);

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



int jks_push(char *name, char *path, string_tokens *tokens_match, char *password, char *passtr, uint64_t period) {
	string *match = string_tokens_join(tokens_match, ",", 1);
	glog(L_DEBUG, "run jks_push with name %s, path %s, match %s, and password/passtr %p/%p\n", name, path, match->s, password, passtr);

	if (!password && !passtr)
	{
		glog(L_INFO, "no set password for jks: %s\n", name);
		return 0;
	}

	lang_options *lo = calloc(1, sizeof(*lo));
	lo->key = strdup(name);
	lo->lang = strdup("so");
	lo->module = strdup("parseJks");
	lo->method = strdup("alligator_call");
	lo->hidden_arg = 1;
	lo->carg = calloc(1, sizeof(context_arg));
	lo->carg->log_level = ac->system_carg->log_level;
	lo->carg->no_metric = 1;

	if (!passtr)
	{
		size_t len = strlen(path) + match->l + strlen(password) + 3;
		passtr = malloc (len + 1);
		snprintf(passtr, len, "%s %s %s", path, match->s, password);
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
	char *path = (char*)json_string_value(jpath);
	size_t path_len = json_string_length(jpath);
	while (path[path_len-1] == '/') --path_len;
	path[path_len] = 0;

	json_t *jmatch = json_object_get(x509, "match");
	if (!jmatch) {
		glog(L_INFO, "not specified param 'match' in x509 context\n");
		return 0;
	}

	uint64_t match_size = json_array_size(jmatch);
	string_tokens *tokens_match = NULL;
	for (uint64_t i = 0; i < match_size; i++)
	{
		if (!tokens_match)
			tokens_match = string_tokens_new();
		json_t *str = json_array_get(jmatch, i);
		string_tokens_push_dupn(tokens_match, (char*)json_string_value(str), json_string_length(str));
	}

	json_t *jpassword = json_object_get(x509, "password");
	char *password = (char*)json_string_value(jpassword);

	json_t *jtype = json_object_get(x509, "type");
	char *type = (char*)json_string_value(jtype);

	uint64_t period = 10000;
	json_t *json_period = json_object_get(x509, "period");
	if (json_period)
		period = get_ms_from_human_range(json_string_value(json_period), json_string_length(json_period));

	if (type && !strcmp(type, "jks"))
		return jks_push(name, path, tokens_match, password, NULL, period);
	else
		return tls_fs_push(name, path, tokens_match, password, type, period);
}
