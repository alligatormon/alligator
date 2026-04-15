#include "amtail/type.h"
#include "common/logs.h"
#include "external/amtail/compile.h"
#include "external/amtail/file.h"
#include "external/amtail/log.h"
#include "main.h"
#include <string.h>

extern aconf *ac;

int amtail_push(json_t *amtail)
{
	if (!amtail_init())
		return 0;

	json_t *jname = json_object_get(amtail, "name");
	if (!jname) {
		glog(L_ERROR, "amtail_push: not specified 'name' option\n");
		return 0;
	}
	char *name = (char*)json_string_value(jname);
	if (!name || !*name)
		return 0;

	json_t *jscript = json_object_get(amtail, "script");
	char *script_path = (char*)json_string_value(jscript);
	if (!script_path || !*script_path)
	{
		glog(L_ERROR, "amtail_push: not specified 'script' option in '%s'\n", name);
		return 0;
	}

	file *f = readfile(script_path);
	if (!f)
	{
		glog(L_ERROR, "amtail_push: cannot read script '%s'\n", script_path);
		return 0;
	}

	string *script_src = string_init_alloc(f->mem, f->size);
	releasefile(f);

	amtail_log_level amtail_ll = {0};
	amtail_bytecode *bytecode = amtail_compile(name, script_src, amtail_ll);
	string_free(script_src);
	if (!bytecode)
	{
		glog(L_ERROR, "amtail_push: compile failed for '%s'\n", name);
		return 0;
	}

	amtail_node *old = amtail_node_get(name);
	if (old)
		amtail_del(name);

	amtail_node *an = calloc(1, sizeof(*an));
	an->name = strdup(name);
	an->script = strdup(script_path);
	an->bytecode = bytecode;
	uv_mutex_init(&an->lock);

	json_t *jkey = json_object_get(amtail, "key");
	char *key = (char*)json_string_value(jkey);
	if (key && *key)
		an->key = strdup(key);

	alligator_ht_insert(ac->amtail, &(an->node), an, tommy_strhash_u32(0, an->name));

	return 1;
}
