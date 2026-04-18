#include "amtail/type.h"
#include "common/logs.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "external/amtail/compile.h"
#include "external/amtail/parser.h"
#include "external/amtail/variables.h"
#include "external/amtail/vm.h"
#include "metric/labels.h"
#include "metric/namespace.h"
#include "main.h"
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
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

typedef struct amtail_metric_ctx {
	context_arg *carg;
	alligator_ht *variables;
} amtail_metric_ctx;

static alligator_ht *amtail_labels_dup_or_new(alligator_ht *labels)
{
	if (labels)
		return labels_dup(labels);
	return alligator_ht_init(NULL);
}

static void amtail_histogram_emit(amtail_variable *var, alligator_ht *base_labels, context_arg *carg)
{
	if (!var || !var->export_name || !var->export_name->s || !carg || !var->bucket_hits)
		return;

	char metric_name[512];
	char le_buf[128];

	for (uint8_t i = 0; i < var->bucket_count; ++i)
	{
		if (!var->bucket || !var->bucket[i] || !var->bucket[i]->s)
			continue;

		const char *le_value = var->bucket[i]->s;
		char *end = NULL;
		double bound = strtod(le_value, &end);
		if (end && *end == '\0' && fabs(bound) >= 1000000.0)
		{
			snprintf(le_buf, sizeof(le_buf), "%.6e", bound);
			le_value = le_buf;
		}

		alligator_ht *bucket_labels = amtail_labels_dup_or_new(base_labels);
		labels_hash_insert_nocache(bucket_labels, "le", (char*)le_value);

		snprintf(metric_name, sizeof(metric_name), "%s_bucket", var->export_name->s);
		uint64_t bucket_value = var->bucket_hits[i];
		metric_add(metric_name, bucket_labels, &bucket_value, DATATYPE_UINT, carg);
	}

	alligator_ht *inf_labels = amtail_labels_dup_or_new(base_labels);
	labels_hash_insert_nocache(inf_labels, "le", "+Inf");
	snprintf(metric_name, sizeof(metric_name), "%s_bucket", var->export_name->s);
	uint64_t inf_value = var->bucket_hits[var->bucket_count];
	metric_add(metric_name, inf_labels, &inf_value, DATATYPE_UINT, carg);

	snprintf(metric_name, sizeof(metric_name), "%s_sum", var->export_name->s);
	metric_add(metric_name, base_labels ? labels_dup(base_labels) : NULL, &var->histogram_sum, DATATYPE_DOUBLE, carg);

	snprintf(metric_name, sizeof(metric_name), "%s_count", var->export_name->s);
	metric_add(metric_name, base_labels ? labels_dup(base_labels) : NULL, &var->histogram_count, DATATYPE_UINT, carg);
}

static char *amtail_value_from_variable(amtail_variable *value_var)
{
	if (!value_var)
		return NULL;

	if (value_var->type == ALLIGATOR_VARTYPE_TEXT && value_var->s && value_var->s->s)
		return strdup(value_var->s->s);
	if (value_var->type == ALLIGATOR_VARTYPE_CONST)
	{
		if (value_var->facttype == ALLIGATOR_FACTTYPE_TEXT && value_var->s && value_var->s->s)
			return strdup(value_var->s->s);
		if (value_var->facttype == ALLIGATOR_FACTTYPE_INT)
		{
			char tmp[64];
			snprintf(tmp, sizeof(tmp), "%"PRId64, value_var->i);
			return strdup(tmp);
		}
		if (value_var->facttype == ALLIGATOR_FACTTYPE_DOUBLE)
		{
			char tmp[64];
			snprintf(tmp, sizeof(tmp), "%.17g", value_var->d);
			return strdup(tmp);
		}
	}
	if (value_var->type == ALLIGATOR_VARTYPE_COUNTER)
	{
		char tmp[64];
		snprintf(tmp, sizeof(tmp), "%"PRId64, value_var->i);
		return strdup(tmp);
	}
	if (value_var->type == ALLIGATOR_VARTYPE_GAUGE)
	{
		char tmp[64];
		snprintf(tmp, sizeof(tmp), "%.17g", value_var->d);
		return strdup(tmp);
	}

	return NULL;
}

static char *amtail_lookup_label_variable(const char *name, alligator_ht *variables)
{
	if (!name || !*name || !variables)
		return NULL;

	size_t name_len = strlen(name);
	amtail_variable *resolved = alligator_ht_search(variables, amtail_variable_compare, (void*)name, amtail_hash((char*)name, name_len));
	if (!resolved && name[0] == '$' && name[1])
	{
		const char *trimmed = name + 1;
		size_t trimmed_len = strlen(trimmed);
		resolved = alligator_ht_search(variables, amtail_variable_compare, (void*)trimmed, amtail_hash((char*)trimmed, trimmed_len));
	}

	return amtail_value_from_variable(resolved);
}

static char *amtail_resolve_label_value(const char *raw, size_t raw_len, alligator_ht *variables)
{
	if (!raw || !raw_len)
		return NULL;

	string *tmp = string_init_alloc((char*)raw, raw_len);
	if (!tmp || !tmp->s)
		return NULL;

	char *start = tmp->s;
	while (*start && isspace((unsigned char)*start))
		++start;

	char *end = tmp->s + strlen(tmp->s);
	while (end > start && isspace((unsigned char)*(end - 1)))
		--end;
	*end = '\0';

	size_t len = strlen(start);
	if (len >= 2 && ((start[0] == '"' && start[len - 1] == '"') || (start[0] == '\'' && start[len - 1] == '\'')))
	{
		start[len - 1] = '\0';
		++start;
	}

	char *resolved = NULL;
	if (*start == '$' && variables)
		resolved = amtail_lookup_label_variable(start, variables);
	if (!resolved)
		resolved = strdup(start);

	string_free(tmp);
	return resolved;
}

static alligator_ht *amtail_variable_make_labels(amtail_variable *var, alligator_ht *variables)
{
	if (!var || !var->by || !var->by_count || !var->by_positions || !var->key)
		return NULL;

	alligator_ht *labels = NULL;
	size_t key_len = strlen(var->key);
	for (uint8_t i = 0; i < var->by_count; ++i)
	{
		if (!var->by[i] || !var->by[i]->s)
			continue;

		size_t start = var->by_positions[i];
		size_t next = var->by_positions[i + 1];
		if (start >= key_len || next <= start + 2)
			continue;
		if (next > key_len + 1)
			next = key_len + 1;

		size_t value_len = next - start - 2;
		if (!value_len)
			continue;

		if (!labels)
			labels = alligator_ht_init(NULL);

		char *value = amtail_resolve_label_value(var->key + start, value_len, variables);
		if (!value)
			continue;
		labels_hash_insert_nocache(labels, var->by[i]->s, value);
		free(value);
	}

	return labels;
}

static void amtail_variable_metric_add(void *funcarg, void *arg)
{
	amtail_metric_ctx *ctx = funcarg;
	amtail_variable *var = arg;
	context_arg *carg = ctx->carg;

	if (!ctx || !carg || !var || !var->export_name || !var->export_name->s || var->is_template)
		return;

	alligator_ht *labels = amtail_variable_make_labels(var, ctx->variables);
	int8_t dtype = DATATYPE_NONE;
	void *metric_value = NULL;

	int64_t i_value = 0;
	double d_value = 0;

	if (var->type == ALLIGATOR_VARTYPE_COUNTER)
	{
		i_value = var->i;
		dtype = DATATYPE_INT;
		metric_value = &i_value;
	}
	else if (var->type == ALLIGATOR_VARTYPE_GAUGE || var->type == ALLIGATOR_VARTYPE_HISTOGRAM)
	{
		d_value = var->d;
		dtype = DATATYPE_DOUBLE;
		metric_value = &d_value;
	}

	if (var->type == ALLIGATOR_VARTYPE_HISTOGRAM)
	{
		amtail_histogram_emit(var, labels, ctx->carg);
		if (labels)
			labels_hash_free(labels);
	}
	else if (metric_value && dtype != DATATYPE_NONE && dtype != DATATYPE_STRING) {
		if (dtype == DATATYPE_INT)
			carglog(carg, L_DEBUG, "mtail metric_add int: %s, %"PRId64", %d, %p\n", var->export_name->s, *((int64_t*)metric_value), dtype, carg);
		else if (dtype == DATATYPE_UINT)
			carglog(carg, L_DEBUG, "mtail metric_add uint: %s, %"PRIu64", %d, %p\n", var->export_name->s, *((uint64_t*)metric_value), dtype, carg);
		else if (dtype == DATATYPE_DOUBLE)
			carglog(carg, L_DEBUG, "mtail metric_add double: %s, %.17g, %d, %p\n", var->export_name->s, *((double*)metric_value), dtype, carg);
		metric_add(var->export_name->s, labels, metric_value, dtype, ctx->carg);
	}
	else if (labels)
		labels_hash_free(labels);
}

static void amtail_variables_to_metrics(alligator_ht *variables, context_arg *carg)
{
	if (!variables || !carg)
		return;

	amtail_metric_ctx ctx = { .carg = carg, .variables = variables };
	alligator_ht_foreach_arg(variables, amtail_variable_metric_add, &ctx);
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
	if (!carg->amtail_variables)
		carg->amtail_variables = amtail_variables_init();
	string *line = string_new();

	for (size_t i = 0; i < total; ++i)
	{
		if (buf[i] != '\n')
			continue;

		size_t line_len = i - start;
		if (line_len && buf[start + line_len - 1] == '\r')
			--line_len;
		if (line_len)
		{
			string_cat(line, buf + start, line_len);
			if (!amtail_run(an->bytecode, carg->amtail_variables, line, an->amtail_ll))
				rc = 0;
			string_null(line);
		}
		start = i + 1;
	}
	amtail_variables_dump(carg->amtail_variables);

	if (an->tail)
	{
		string_free(an->tail);
		an->tail = NULL;
	}
	if (start < total)
		an->tail = string_init_alloc(buf + start, total - start);

	amtail_variables_to_metrics(carg->amtail_variables, carg);

	free(buf);
	uv_mutex_unlock(&an->lock);
	carg->parser_status = rc ? 1 : 0;
}

void amtail_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("mtail");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);

	actx->handler[0].name = amtail_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = amtail_mesg;
	strlcpy(actx->handler[0].key, "mtail", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
