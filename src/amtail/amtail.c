#include "amtail/type.h"
#include "common/logs.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "external/amtail/compile.h"
#include "external/amtail/parser.h"
#include "external/amtail/vm.h"
#include "main.h"
#include <stdlib.h>
#include <string.h>

extern aconf *ac;

int amtail_node_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((amtail_node*)obj)->name;
	return strcmp(s1, s2);
}

static void amtail_node_get_any_foreach(void *funcarg, void *arg)
{
	amtail_node **dst = funcarg;
	if (!*dst)
		*dst = arg;
}

amtail_node *amtail_node_get(char *name)
{
	if (!ac || !ac->amtail || !name)
		return NULL;
	return alligator_ht_search(ac->amtail, amtail_node_compare, name, tommy_strhash_u32(0, name));
}

amtail_node *amtail_node_get_any(void)
{
	amtail_node *an = NULL;
	if (!ac || !ac->amtail)
		return NULL;
	alligator_ht_foreach_arg(ac->amtail, amtail_node_get_any_foreach, &an);
	return an;
}

static amtail_node *amtail_node_load_for_carg(context_arg *carg)
{
	if (!carg || !carg->script)
		return NULL;

	char *name = carg->name ? carg->name : carg->script;
	amtail_node *an = amtail_node_get(name);
	if (an)
		return an;

	json_t *tmp = json_object();
	json_array_object_insert(tmp, "name", json_string(name));
	json_array_object_insert(tmp, "script", json_string(carg->script));
	if (carg->key)
		json_array_object_insert(tmp, "key", json_string(carg->key));

	int rc = amtail_push(tmp);
	json_decref(tmp);
	if (!rc)
		return NULL;
	return amtail_node_get(name);
}

void amtail_node_free(amtail_node *an)
{
	if (!an)
		return;
	if (an->tail)
		string_free(an->tail);
	if (an->bytecode)
		amtail_code_free(an->bytecode);
	if (an->variables) {
		alligator_ht_done(an->variables);
		free(an->variables);
	}
	if (an->labels) {
		alligator_ht_done(an->labels);
		free(an->labels);
	}
	if (an->name)
		free(an->name);
	if (an->key)
		free(an->key);
	if (an->script)
		free(an->script);
	uv_mutex_destroy(&an->lock);
	free(an);
}

static void amtail_free_foreach(void *funcarg, void *arg)
{
	(void)funcarg;
	amtail_node *an = arg;
	amtail_node_free(an);
}

int amtail_init()
{
	static uint8_t vm_initialized = 0;
	if (!ac)
		return 0;

	if (!ac->amtail)
	{
		ac->amtail = calloc(1, sizeof(alligator_ht));
		alligator_ht_init(ac->amtail);
	}

	if (!vm_initialized)
	{
		amtail_parser_init();
		amtail_vm_init();
		vm_initialized = 1;
	}

	return 1;
}

void amtail_free()
{
	if (!ac || !ac->amtail)
		return;
	alligator_ht_foreach_arg(ac->amtail, amtail_free_foreach, NULL);
	alligator_ht_done(ac->amtail);
	free(ac->amtail);
	ac->amtail = NULL;
}

string* amtail_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
    if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
        return string_init_add_auto(gen_http_query(0, hi->query, "", hi->host, "alligator", hi->auth, NULL, env, proxy_settings, NULL));
    else if (hi->query)
        return string_init_alloc(hi->query, 0);
    else
        return NULL;
}

void amtail_handler(char *metrics, size_t size, context_arg *carg)
{
	amtail_node *an = NULL;
	if (!metrics || !size || !carg)
	{
		if (carg)
			carg->parser_status = 0;
		return;
	}

	if (carg->name)
		an = amtail_node_get(carg->name);
	if (!an)
		an = amtail_node_load_for_carg(carg);
	if (!an)
		an = amtail_node_get_any();

	if (!an || !an->bytecode)
	{
		carglog(carg, L_ERROR, "amtail: no compiled script loaded (set name=... and push script first)\n");
		carg->parser_status = 0;
		return;
	}

	uv_mutex_lock(&an->lock);
	size_t tail_len = an->tail ? an->tail->l : 0;
	size_t total = tail_len + size;
	char *buf = calloc(1, total + 1);
	if (!buf)
	{
		uv_mutex_unlock(&an->lock);
		carg->parser_status = 0;
		return;
	}

	if (tail_len)
		memcpy(buf, an->tail->s, tail_len);
	memcpy(buf + tail_len, metrics, size);

	size_t start = 0;
	int rc = 1;
	for (size_t i = 0; i < total; ++i)
	{
		if (buf[i] != '\n')
			continue;

		size_t line_len = i - start;
		if (line_len && buf[start + line_len - 1] == '\r')
			--line_len;
		if (line_len)
		{
			string *line = string_init_alloc(buf + start, line_len);
			if (!amtail_run(an->bytecode, line))
				rc = 0;
			string_free(line);
		}
		start = i + 1;
	}

	if (an->tail)
	{
		string_free(an->tail);
		an->tail = NULL;
	}
	if (start < total)
		an->tail = string_init_alloc(buf + start, total - start);

	free(buf);
	uv_mutex_unlock(&an->lock);
	carg->parser_status = rc ? 1 : 0;
}

void amtail_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("amtail");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);

	actx->handler[0].name = amtail_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = amtail_mesg;
	strlcpy(actx->handler[0].key, "amtail", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}