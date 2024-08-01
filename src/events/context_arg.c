#include "events/context_arg.h"
#include "main.h"
#include "common/file_stat.h"
#include "common/netlib.h"
#include "resolver/resolver.h"
#include "common/json_parser.h"
#include "common/units.h"
#include "common/logs.h"
extern aconf *ac;

context_arg *carg_copy(context_arg *src)
{
	size_t carg_size = sizeof(context_arg);
	context_arg *carg = malloc(carg_size);
	memcpy(carg, src, carg_size);

	carg->net_tree_acl = patricia_tree_duplicate(src->net_tree_acl);
	carg->net6_tree_acl = patricia_tree_duplicate(src->net6_tree_acl);
	if (src->key)
		carg->key = strdup(src->key);

	if (carg->cluster)
		carg->cluster = strdup(src->cluster);

	if (carg->instance)
		carg->instance = strdup(src->instance);

	if (carg->lang)
		carg->lang = strdup(src->lang);

	if (carg->namespace && carg->namespace_allocated)
		carg->namespace = strdup(src->namespace);

	if (carg->auth_bearer)
	{
		carg->auth_bearer_size = src->auth_bearer_size;
		carg->auth_bearer = calloc(1, sizeof(void*) * carg->auth_bearer_size);

		for (uint64_t i = 0; i < carg->auth_bearer_size; ++i)
			carg->auth_bearer[i] = strdup(src->auth_bearer[i]);
	}

	if (carg->auth_basic)
	{
		carg->auth_basic_size = src->auth_basic_size;
		carg->auth_basic = calloc(1, sizeof(void*) * carg->auth_basic_size);

		for (uint64_t i = 0; i < carg->auth_basic_size; ++i)
			carg->auth_basic[i] = strdup(src->auth_basic[i]);
	}

	if (carg->auth_other)
	{
		carg->auth_other_size = src->auth_other_size;
		carg->auth_other = calloc(1, sizeof(void*) * carg->auth_other_size);

		for (uint64_t i = 0; i < carg->auth_other_size; ++i)
			carg->auth_other[i] = strdup(src->auth_other[i]);
	}

	if (carg->auth_header)
		carg->auth_header = strdup(src->auth_header);

	if (src->env)
		carg->env = env_struct_duplicate(src->env);

	if (src->labels)
		carg->labels = labels_dup(src->labels);

	return carg;
}

void env_struct_free(void *funcarg, void* arg)
{
	env_struct *es = arg;
	//alligator_ht *hash = funcarg;
	//alligator_ht_remove_existing(hash, &(es->node));
	free(es->k);
	free(es->v);
	free(es);
}

void carg_free(context_arg *carg)
{
	carglog(carg, L_DEBUG, "FREE context argument %p with hostname '%s', key '%s'(%p), with mesg '%s'\n", carg, carg->host, carg->key, carg->key, carg->mesg);

	if (carg->mesg)
		free(carg->mesg);

	if (carg->key)
		free(carg->key);

	if (carg->buffer)
		free(carg->buffer);

	if (carg->remote)
        	free(carg->remote);

	if (carg->local)
        	free(carg->local);

	if (carg->query_url)
		free(carg->query_url);

	if (carg->url)
		free(carg->url);

	if (carg->name)
		free(carg->name);

	if (carg->lang)
		free(carg->lang);

	if (carg->checksum)
		free(carg->checksum);

	if (carg->full_body)
		string_free(carg->full_body);

	if (carg->args)
	{
		char **args = carg->args;
		for(uint64_t i = 0; args[i]; ++i) {
			free(args[i]);
		}
		free(carg->args);
		carg->args = NULL;
	}

	if (carg->work_dir)
		string_free(carg->work_dir);

	//if (carg->dest)
	//	free(carg->dest);

	if (carg->recv)
		free(carg->recv);

	if (carg->cluster)
		free(carg->cluster);

	if (carg->instance)
		free(carg->instance);

	if (carg->pquery)
		free(carg->pquery);

	if (carg->tls_ca_file)
		free(carg->tls_ca_file);

	if (carg->tls_key_file)
		free(carg->tls_key_file);

	if (carg->tls_cert_file)
		free(carg->tls_cert_file);

	if (carg->stdin_s)
		free(carg->stdin_s);

	if (carg->namespace && carg->namespace_allocated) {
		free(carg->namespace);
		carg->namespace_allocated = 0;
	}

	if (carg->auth_header)
		free(carg->auth_header);

	if (carg->q_request_time)
		free_percentile_buffer(carg->q_request_time);
	if (carg->q_read_time)
		free_percentile_buffer(carg->q_read_time);
	if (carg->q_connect_time)
		free_percentile_buffer(carg->q_connect_time);


	if (carg->env)
	{
		alligator_ht_foreach_arg(carg->env, env_struct_free, carg->env);
		alligator_ht_done(carg->env);
		free(carg->env);
	}

	if (carg->auth_basic)
	{
		for (uint64_t i = 0; i < carg->auth_basic_size; ++i)
			if (carg->auth_basic[i])
				free(carg->auth_basic[i]);
		free(carg->auth_basic);
	}

	if (carg->auth_bearer)
	{
		for (uint64_t i = 0; i < carg->auth_bearer_size; ++i)
			if (carg->auth_bearer[i])
				free(carg->auth_bearer[i]);
		free(carg->auth_bearer);
	}

	if (carg->auth_other)
	{
		for (uint64_t i = 0; i < carg->auth_other_size; ++i)
			if (carg->auth_other[i])
				free(carg->auth_other[i]);
		free(carg->auth_other);
	}

	patricia_free(carg->net_tree_acl);
	patricia_free(carg->net6_tree_acl);
	labels_hash_free(carg->labels);
	// TODO: free carg->socket
	// TODO: free carg->mm

	free(carg);
}

void aconf_mesg_set(context_arg *carg, char *mesg, size_t mesg_len)
{
	if (!mesg_len && mesg)
		mesg_len = strlen(mesg);

	carg->buffer = malloc(sizeof(uv_buf_t));
	carg->mesg = mesg; // old scheme
	carg->mesg_len = mesg_len; // old scheme
	carg->request_buffer = uv_buf_init(mesg, mesg_len); // new scheme
	carg->buffer->base = carg->mesg; // old scheme
	carg->buffer->len = carg->mesg_len; // old scheme

	if (mesg)
		carg->write = 1;
	else
		carg->write = 0;

}

int env_struct_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((env_struct*)obj)->k;
        return strcmp(s1, s2);
}

void env_struct_push_alloc(alligator_ht* hash, char *k, char *v)
{
	if (!hash || !k || !v)
		return;

	uint32_t key_hash = tommy_strhash_u32(0, k);
	env_struct *es = alligator_ht_search(hash, env_struct_compare, k, key_hash);
	if (!es)
	{
		es = calloc(1, sizeof(*es));
		es->k = strdup(k);
		es->v = strdup(v);
		alligator_ht_insert(hash, &(es->node), es, key_hash);
	}
	glog(L_ERROR, "duplicate header key %s\n", k);
}

alligator_ht *env_struct_parser(json_t *root)
{
	json_t *json_env = json_object_get(root, "env");

	const char *env_name;
	json_t *env_jkey;
	alligator_ht *env = alligator_ht_init(NULL);

	json_object_foreach(json_env, env_name, env_jkey)
	{
		char *env_key = (char*)json_string_value(env_jkey);
		env_struct_push_alloc(env, (char*)env_name, env_key);
	}
	return env;
}

void env_struct_duplicate_foreach(void *funcarg, void* arg)
{
	env_struct *es = arg;
	alligator_ht *hash = funcarg;
	env_struct_push_alloc(hash, es->k, es->v);
}

alligator_ht* env_struct_duplicate(alligator_ht *src)
{
	if (!src)
		return src;

	alligator_ht *dst = alligator_ht_init(NULL);
	alligator_ht_foreach_arg(src, env_struct_duplicate_foreach, dst);
	return dst;
}

void env_struct_dump_foreach(void *funcarg, void* arg)
{
	env_struct *es = arg;
	json_t *env = funcarg;
	json_t *value = json_string(es->v);

	json_array_object_insert(env, es->k, value);
}

json_t* env_struct_dump(alligator_ht *src)
{
	if (!src)
		return NULL;

	json_t *env = json_object();
	alligator_ht_foreach_arg(src, env_struct_dump_foreach, env);
	return env;
}

void env_free(alligator_ht *env)
{
	alligator_ht_foreach_arg(env, env_struct_free, env);
	alligator_ht_done(env);
	free(env);
}

void parse_add_label(context_arg *carg, json_t *root) {
	json_t *json_add_label = json_object_get(root, "add_label");

	const char *name;
	json_t *jkey;
	json_object_foreach(json_add_label, name, jkey)
	{
		char *key = (char*)json_string_value(jkey);

		if (!carg->labels)
		{
			carg->labels = alligator_ht_init(NULL);
		}

		labels_hash_insert_nocache(carg->labels, (char*)name, key);
	}
}

context_arg* context_arg_json_fill(json_t *root, host_aggregator_info *hi, void *handler, char *parser_name, char *mesg, size_t mesg_len, void *data, void *expect_function, uint8_t headers_pass, uv_loop_t *loop, alligator_ht *env, uint64_t follow_redirects, char *stdin_s, size_t stdin_l)
{
	context_arg *carg = calloc(1, sizeof(*carg));
	carg->ttl = -1;
	carglog(carg, L_INFO, "allocated context argument %p with hostname '%s' with mesg '%s'\n", carg, carg->host, carg->mesg);

	aconf_mesg_set(carg, mesg, mesg_len);

	carg->headers_pass = headers_pass;
	carg->url = strdup(hi->url);
	if (hi->user)
		strlcpy(carg->user, hi->user, 1024);
	if (hi->pass)
		strlcpy(carg->password, hi->pass, 1024);
	strlcpy(carg->host, hi->host, HOSTHEADER_SIZE); // new scheme
	strlcpy(carg->port, hi->port, 6);
	carg->numport = strtoull(hi->port, NULL, 10);
	if (carg->timeout < 1)
		carg->timeout = 5000;
	carg->proto = hi->proto;
	carg->transport = hi->transport;
	carg->is_http_query = 0;
	carg->follow_redirects = follow_redirects;
	carg->tls = hi->tls;
	carg->transport_string = hi->transport_string;
	carg->stdin_s = stdin_s;
	carg->stdin_l = stdin_l;
	carg->loop = loop;
	if (hi->query)
		carg->query_url = strdup(hi->query);
	else
		carg->query_url = strdup("");
	carg->parser_handler = handler;
	carg->parser_name = parser_name;
	//*(carg->buffer) = uv_buf_init(carg->mesg, carg->mesg_len);
	carg->buflen = 1;
	carg->expect_function = expect_function;
	carg->data = data;
	//carg->tt_timer = malloc(sizeof(uv_timer_t));
	carg->write = 2;
	carg->curr_ttl = -1;
	carg->buffer_request_size = 1553500;
	carg->buffer_response_size = 1553500;
	carg->full_body = string_init(1553500);
	if (hi->proto == APROTO_HTTPS || hi->proto == APROTO_TLS)
		carg->tls = 1;
	else
		carg->tls = 0;

	json_t *json_key = json_object_get(root, "key");
	char *str_key = (char*)json_string_value(json_key);
	if (str_key)
		carg->key = strdup(str_key);

	json_t *json_name = json_object_get(root, "name");
	char *str_name = (char*)json_string_value(json_name);
	if (str_name)
		carg->name = strdup(str_name);

	json_t *json_lang = json_object_get(root, "lang");
	char *str_lang = (char*)json_string_value(json_lang);
	if (str_lang)
		carg->lang = strdup(str_lang);

	json_t *json_cert = json_object_get(root, "tls_certificate");
	char *str_cert = (char*)json_string_value(json_cert);
	if (str_cert)
		carg->tls_cert_file = strdup(str_cert);

	json_t *json_tls_server_name = json_object_get(root, "tls_server_name");
	char *tls_server_name = (char*)json_string_value(json_tls_server_name);
	if (tls_server_name)
		carg->tls_server_name = strdup(tls_server_name);

	json_t *json_tls_key = json_object_get(root, "tls_key");
	char *str_tls_key = (char*)json_string_value(json_tls_key);
	if (str_tls_key)
		carg->tls_key_file = strdup(str_tls_key);

	json_t *json_ca = json_object_get(root, "tls_ca");
	char *str_ca = (char*)json_string_value(json_ca);
	if (str_ca)
		carg->tls_ca_file = strdup(str_ca);

	json_t *json_timeout = json_object_get(root, "timeout");
	int64_t int_timeout = json_integer_value(json_timeout);
	if (json_timeout)
		carg->timeout = int_timeout;

	json_t *json_ttl = json_object_get(root, "ttl");
	int64_t int_ttl = json_integer_value(json_ttl);
	if (json_ttl)
		carg->ttl = int_ttl;

	json_t *json_file_stat = json_object_get(root, "file_stat");
	carg->file_stat = json_boolean_value(json_file_stat);
	char *flstt = (char*)json_string_value(json_file_stat);
	if (flstt && !strcmp(flstt, "true"))
		carg->file_stat = 1;

	json_t *json_checksum = json_object_get(root, "checksum");
	if (json_checksum)
		carg->checksum = strdup((char*)json_string_value(json_checksum));

	json_t *json_calc_lines = json_object_get(root, "calc_lines");
	carg->calc_lines = json_boolean_value(json_calc_lines);
	char *clc = (char*)json_string_value(json_calc_lines);
	if (clc && !strcmp(clc, "true"))
		carg->calc_lines = 1;


	json_t *json_notify = json_object_get(root, "notify");
	carg->notify = json_boolean_value(json_notify);
	char *ntf = (char*)json_string_value(json_notify);
	if (ntf && !strcmp(ntf, "true"))
		carg->notify = 1;

	carg->pingloop = 1;
	json_t *json_pingloop = json_object_get(root, "pingloop");
	int64_t int_pingloop = json_integer_value(json_pingloop);
	if (json_pingloop)
		carg->pingloop = int_pingloop;

	json_t *json_state = json_object_get(root, "state");
	char *state = (char*)json_string_value(json_state);
	if (state)
	{
		if (!strcmp(state, "begin"))
			carg->state = FILESTAT_STATE_BEGIN;
		else if (!strcmp(state, "save"))
			carg->state = FILESTAT_STATE_SAVE;
		if (!strcmp(state, "forget"))
			carg->state = FILESTAT_STATE_FORGET;
	}

	json_t *json_log_level = json_object_get(root, "log_level");
	if (json_log_level && json_typeof(json_log_level) == JSON_STRING)
		carg->log_level = get_log_level_by_name(json_string_value(json_log_level), json_string_length(json_log_level));
	else if (json_log_level)
		carg->log_level = json_integer_value(json_log_level);

	parse_add_label(carg, root);

	json_t *json_stdin = json_object_get(root, "stdin");
	if (json_stdin)
	{
		carg->stdin_s = strdup(json_string_value(json_stdin));
		carg->stdin_l = json_string_length(json_stdin);
	}

	// duplicate env
	carg->env = env_struct_duplicate(env);

	json_t *json_cluster = json_object_get(root, "cluster");
	if (json_cluster)
	{
		carg->cluster = strdup(json_string_value(json_cluster));
		carg->cluster_size = json_string_length(json_cluster);
	}

	json_t *json_instance = json_object_get(root, "instance");
	if (json_instance)
	{
		carg->instance = strdup(json_string_value(json_instance));
	}

	json_t *json_pquery = json_object_get(root, "pquery");
	if (json_pquery)
	{
		carg->pquery = strdup(json_string_value(json_pquery));
	}

	json_t *json_namespace = json_object_get(root, "namespace");
	if (json_namespace)
	{
		carg->namespace = strdup(json_string_value(json_namespace));
        carg->namespace_allocated = 1;
        insert_namespace(carg->namespace);
	}

	json_t *json_period = json_object_get(root, "period");
	if (json_period)
	{
		carg->period = get_ms_from_human_range(json_string_value(json_period), json_string_length(json_period));
	}

	json_t *json_resolve = json_object_get(root, "resolve");
	if (json_resolve)
	{
		char *buf = calloc(1, 1024);
		json_t *json_rrtype = json_object_get(root, "type");
		char *rrtype;
		if (json_rrtype)
			rrtype = (char*)json_string_value(json_rrtype);
		else
			rrtype = "any";

		carg->resolver = 1;
		//resolver_carg_set_transport(carg);

		carg->key = malloc(255);
		carg->data = strdup(json_string_value(json_resolve));
		snprintf(carg->key, 254, "%s://%s/%s:%s:%s", carg->transport_string, carg->host, (char*)carg->data, "IN", rrtype);

		uint16_t transaction_id;
		uint64_t buflen = dns_init_strtype(carg->data, buf, rrtype, &transaction_id);
		carg->rrtype = rrtype;
		carg->rrclass = 1;
		carg->packets_id = transaction_id;
		aconf_mesg_set(carg, buf, buflen);
	}

	carg->q_request_time = init_percentile_buffer(percentile_init_3n(99, 95, 90), 3);
	carg->q_read_time = init_percentile_buffer(percentile_init_3n(99, 95, 90), 3);
	carg->q_connect_time = init_percentile_buffer(percentile_init_3n(99, 95, 90), 3);

	return carg;
}

void carglog(context_arg *carg, int priority, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	wrlog(carg->log_level, priority, format, args);
	va_end(args);
}
