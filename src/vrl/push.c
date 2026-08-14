#include "vrl/type.h"
#include "common/logs.h"
#include "external/amtail/file.h"
#include "main.h"
#include <stdlib.h>
#include <string.h>

extern aconf *ac;

static char *read_script_file(const char *path, size_t *out_len)
{
	file *f = readfile((char *)path);
	if (!f)
		return NULL;
	char *buf = malloc(f->size + 1);
	if (!buf) {
		releasefile(f);
		return NULL;
	}
	memcpy(buf, f->mem, f->size);
	buf[f->size] = '\0';
	if (out_len)
		*out_len = f->size;
	releasefile(f);
	return buf;
}

static avrl_log_level parse_log_levels(json_t *cfg)
{
	avrl_log_level ll = {0};
	const char *keys[] = {
	    "log_level_lexer", "log_level_parser", "log_level_compiler",
	    "log_level_vm", "log_level_stdlib", "log_level_pcre"};
	uint8_t *dsts[] = {&ll.lexer, &ll.parser, &ll.compiler, &ll.vm, &ll.stdlib, &ll.pcre};
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		json_t *j = json_object_get(cfg, keys[i]);
		if (json_is_string(j))
			*dsts[i] = (uint8_t)get_log_level_by_name(
			    json_string_value(j), json_string_length(j));
	}
	return ll;
}

static int parse_multiline(json_t *cfg, vrl_node *vn, char **err)
{
	json_t *ml = json_object_get(cfg, "multiline");
	if (!ml || !json_is_object(ml))
		return 1;

	json_t *jmode = json_object_get(ml, "mode");
	json_t *jstart = json_object_get(ml, "start_pattern");
	json_t *jcond = json_object_get(ml, "condition_pattern");
	json_t *jpat = json_object_get(ml, "pattern");
	const char *mode = json_is_string(jmode) ? json_string_value(jmode) : NULL;
	const char *start = json_is_string(jstart) ? json_string_value(jstart) : NULL;
	const char *cond = json_is_string(jcond) ? json_string_value(jcond) : NULL;
	const char *pat = json_is_string(jpat) ? json_string_value(jpat) : NULL;

	if ((!start || !cond) && pat) {
		start = pat;
		cond = pat;
		if (!mode)
			mode = "halt_before";
	}
	if (!mode || !start || !cond) {
		if (err)
			*err = strdup("multiline requires mode and "
				      "start_pattern+condition_pattern (or legacy pattern)");
		return 0;
	}
	alligator_ml_mode m;
	if (alligator_ml_mode_from_str(mode, &m) != 0) {
		if (err)
			*err = strdup("multiline: unknown mode "
				      "(continue_through|continue_past|halt_before|halt_with)");
		return 0;
	}
	vn->ml_mode = m;
	vn->ml_start_pattern = strdup(start);
	vn->ml_condition_pattern = strdup(cond);
	vn->ml_enabled = 1;
	return 1;
}

int vrl_push(json_t *cfg)
{
	if (!vrl_engine_init())
		return 0;

	json_t *jname = json_object_get(cfg, "name");
	if (!jname || !json_is_string(jname)) {
		glog(L_ERROR, "vrl_push: missing 'name'\n");
		return 0;
	}
	const char *name = json_string_value(jname);
	if (!name || !*name)
		return 0;

	json_t *jscript = json_object_get(cfg, "script");
	json_t *jprog = json_object_get(cfg, "program");
	const char *script_path = json_is_string(jscript) ? json_string_value(jscript) : NULL;
	const char *program_inline = json_is_string(jprog) ? json_string_value(jprog) : NULL;

	char *src = NULL;
	size_t src_len = 0;
	if (program_inline && *program_inline) {
		src_len = strlen(program_inline);
		src = strdup(program_inline);
	} else if (script_path && *script_path) {
		src = read_script_file(script_path, &src_len);
		if (!src) {
			glog(L_ERROR, "vrl_push: cannot read script '%s'\n", script_path);
			return 0;
		}
	} else {
		glog(L_ERROR, "vrl_push: need 'script' or 'program' for '%s'\n", name);
		return 0;
	}

	avrl_log_level ll = parse_log_levels(cfg);
	vrl_program *prog = vrl_compile(src, src_len, ll);
	free(src);
	if (!prog || prog->err) {
		glog(L_ERROR, "vrl_push: compile failed for '%s': %s\n",
		     name, prog && prog->err ? prog->err : "unknown");
		if (prog)
			vrl_program_free(prog);
		return 0;
	}

	vrl_node *old = vrl_node_get((char *)name);
	if (old)
		vrl_del((char *)name);

	vrl_node *vn = calloc(1, sizeof(*vn));
	vn->name = strdup(name);
	vn->prog = prog;
	vn->ll = ll;
	if (script_path && *script_path)
		vn->script = strdup(script_path);
	if (program_inline && *program_inline)
		vn->program = strdup(program_inline);

	json_t *jkey = json_object_get(cfg, "key");
	if (json_is_string(jkey) && json_string_value(jkey)[0])
		vn->key = strdup(json_string_value(jkey));

	char *ml_err = NULL;
	if (!parse_multiline(cfg, vn, &ml_err)) {
		glog(L_ERROR, "vrl_push: %s\n", ml_err ? ml_err : "multiline error");
		free(ml_err);
		vrl_node_free(vn);
		return 0;
	}

	uv_mutex_init(&vn->lock);
	alligator_ht_insert(ac->vrl, &(vn->node), vn, tommy_strhash_u32(0, vn->name));
	glog(L_INFO, "vrl_push: compiled '%s'%s%s\n", name,
	     vn->ml_enabled ? " (multiline)" : "",
	     vn->script ? "" : " (inline)");
	return 1;
}
