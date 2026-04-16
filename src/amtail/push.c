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

	amtail_log_level amtail_ll = {
		.parser = 0,
		.lexer = 0,
		.generator = 0,
		.compiler = 0,
		.vm = 0,
		.pcre = 0,
	};
	json_t *jparser = json_object_get(amtail, "log_level_parser");
	if (json_is_string(jparser))
		amtail_ll.parser = (uint8_t)get_log_level_by_name(json_string_value(jparser), json_string_length(jparser));

	json_t *jlexer = json_object_get(amtail, "log_level_lexer");
	if (json_is_string(jlexer))
		amtail_ll.lexer = (uint8_t)get_log_level_by_name(json_string_value(jlexer), json_string_length(jlexer));

	json_t *jgenerator = json_object_get(amtail, "log_level_generator");
	if (json_is_string(jgenerator))
		amtail_ll.generator = (uint8_t)get_log_level_by_name(json_string_value(jgenerator), json_string_length(jgenerator));

	json_t *jcompiler = json_object_get(amtail, "log_level_compiler");
	if (json_is_string(jcompiler))
		amtail_ll.compiler = (uint8_t)get_log_level_by_name(json_string_value(jcompiler), json_string_length(jcompiler));

	json_t *jvm = json_object_get(amtail, "log_level_vm");
	if (json_is_string(jvm))
		amtail_ll.vm = (uint8_t)get_log_level_by_name(json_string_value(jvm), json_string_length(jvm));

	json_t *jpcre = json_object_get(amtail, "log_level_pcre");
	if (json_is_string(jpcre))
		amtail_ll.pcre = (uint8_t)get_log_level_by_name(json_string_value(jpcre), json_string_length(jpcre));
    puts("amtail_push: compiling...");

    string *script = string_init_dup((char*)script_path);
    string_tokens *tokens = amtail_lex(script, (char*)script_path, amtail_ll);
    if (!tokens)
    {
        printf("[FAIL][RUN] lex failed: %s\n", script_path);
        string_free(script);
        return 1;
    }
    amtail_ast *ast = amtail_parser(tokens, (char*)script_path, amtail_ll);
    if (!ast)
    {
        printf("[FAIL][RUN] parser failed: %s\n", script_path);
        string_tokens_free(tokens);
        string_free(script);
        return 1;
    }
    amtail_bytecode *bytecode = amtail_code_generator(ast, amtail_ll);
    if (!bytecode)
    {
        printf("[FAIL][RUN] generator failed: %s\n", script_path);
        amtail_ast_free(ast);
        string_tokens_free(tokens);
        string_free(script);
        return 1;
    }

    printf("amtail_push: compiled '%s', result %p\n", name, bytecode);
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
    an->amtail_ll = amtail_ll;
	uv_mutex_init(&an->lock);

	json_t *jkey = json_object_get(amtail, "key");
	char *key = (char*)json_string_value(jkey);
	if (key && *key)
		an->key = strdup(key);

	alligator_ht_insert(ac->amtail, &(an->node), an, tommy_strhash_u32(0, an->name));

	return 1;
}
