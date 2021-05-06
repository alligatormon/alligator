#include "events/context_arg.h"
#include "main.h"
#include "common/file_stat.h"
extern aconf *ac;

context_arg *carg_copy(context_arg *src)
{
	size_t carg_size = sizeof(context_arg);
	context_arg *carg = malloc(carg_size);
	memcpy(carg, src, carg_size);
	//printf("carg_size is %zu\n", carg_size);
	return carg;
}

void env_struct_free(void *funcarg, void* arg)
{
	env_struct *es = arg;
	tommy_hashdyn *hash = funcarg;
	tommy_hashdyn_remove_existing(hash, &(es->node));
	free(es->k);
	free(es->v);
	free(es);
}

void carg_free(context_arg *carg)
{
	if (ac->log_level > 2)
		printf("FREE context argument %p with hostname '%s', key '%s', with mesg '%s'\n", carg, carg->host, carg->key, carg->mesg);

	if (carg->mesg)
		free(carg->mesg);

	if (carg->key)
		free(carg->key);

	if (carg->net_acl)
		free(carg->net_acl);

	if (carg->buffer)
		free(carg->buffer);

	if (carg->remote)
        	free(carg->remote);

	if (carg->local)
        	free(carg->local);

	if (carg->query_url)
		free(carg->query_url);

	string_free(carg->full_body);

	tommy_hashdyn_foreach_arg(carg->env, env_struct_free, carg->env);
	tommy_hashdyn_done(carg->env);
	free(carg->env);
	// TODO: free carg->labels
	// TODO: free carg->env
	// TODO: free carg->name
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

void env_struct_push_alloc(tommy_hashdyn* hash, char *k, char *v)
{
	if (!hash || !k || !v)
		return;

	uint32_t key_hash = tommy_strhash_u32(0, k);
	env_struct *es = tommy_hashdyn_search(hash, env_struct_compare, k, key_hash);
	if (!es)
	{
		es = calloc(1, sizeof(*es));
		es->k = strdup(k);
		es->v = strdup(v);
		tommy_hashdyn_insert(hash, &(es->node), es, key_hash);
	}
	else if (ac->log_level > 1)
		printf("duplicate header key %s\n", k);
}

tommy_hashdyn *env_struct_parser(json_t *root)
{
	json_t *json_env = json_object_get(root, "env");

	const char *env_name;
	json_t *env_jkey;
	tommy_hashdyn *env = NULL;
	env = malloc(sizeof(tommy_hashdyn));
	tommy_hashdyn_init(env);

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
	tommy_hashdyn *hash = funcarg;
	env_struct_push_alloc(hash, es->k, es->v);
}

tommy_hashdyn* env_struct_duplicate(tommy_hashdyn *src)
{
	if (!src)
		return src;

	tommy_hashdyn *dst = malloc(sizeof(*dst));
	tommy_hashdyn_init(dst);
	tommy_hashdyn_foreach_arg(src, env_struct_duplicate_foreach, dst);
	return dst;
}

context_arg* context_arg_json_fill(json_t *root, host_aggregator_info *hi, void *handler, char *parser_name, char *mesg, size_t mesg_len, void *data, void *expect_function, uint8_t headers_pass, uv_loop_t *loop, tommy_hashdyn *env, uint64_t follow_redirects)
{
	context_arg *carg = calloc(1, sizeof(*carg));
	carg->ttl = -1;
	if (ac->log_level > 2)
		printf("allocated context argument %p with hostname '%s' with mesg '%s'\n", carg, carg->host, carg->mesg);

	aconf_mesg_set(carg, mesg, mesg_len);

	carg->headers_pass = headers_pass;
	carg->url = hi->url;
	if (hi->user)
		strlcpy(carg->user, hi->user, 1024);
	if (hi->pass)
		strlcpy(carg->password, hi->pass, 1024);
	carg->hostname = hi->host; // old scheme
	strlcpy(carg->host, hi->host, 1024); // new scheme
	strlcpy(carg->port, hi->port, 6);
	if (carg->timeout < 1)
		carg->timeout = 5000;
	carg->proto = hi->proto;
	carg->transport = hi->transport;
	carg->is_http_query = 0;
	carg->follow_redirects = follow_redirects;
	carg->tls = hi->tls;
	carg->transport_string = hi->transport_string;
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

	json_t *json_cert = json_object_get(root, "tls_certificate");
	char *str_cert = (char*)json_string_value(json_cert);
	if (str_cert)
		carg->tls_cert_file = strdup(str_cert);

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

	json_t *json_file_stat = json_object_get(root, "file_stat");
	carg->file_stat = json_boolean_value(json_file_stat);

	json_t *json_checksum = json_object_get(root, "checksum");
	if (json_checksum)
		carg->checksum = strdup((char*)json_string_value(json_checksum));

	json_t *json_calc_lines = json_object_get(root, "calc_lines");
	carg->calc_lines = json_boolean_value(json_calc_lines);

	json_t *json_notify = json_object_get(root, "notify");
	carg->notify = json_boolean_value(json_notify);

	json_t *json_state = json_object_get(root, "state");
	char *state = (char*)json_string_value(json_state);
	if (state)
	{
		if (!strcmp(state, "begin"))
			carg->state = FILESTAT_STATE_BEGIN;
		else if (!strcmp(state, "save"))
			carg->state = FILESTAT_STATE_SAVE;
	}

	json_t *json_log_level = json_object_get(root, "log_level");
	if (json_log_level)
	{
		carg->log_level = json_integer_value(json_log_level);
	}

	json_t *json_add_label = json_object_get(root, "add_label");

	const char *name;
	json_t *jkey;
	json_object_foreach(json_add_label, name, jkey)
	{
		char *key = (char*)json_string_value(jkey);

		if (!carg->labels)
		{
			carg->labels = malloc(sizeof(tommy_hashdyn));
			tommy_hashdyn_init(carg->labels);
		}

		labels_hash_insert_nocache(carg->labels, (char*)name, key);
	}

	carg->env = env;

	return carg;
}
