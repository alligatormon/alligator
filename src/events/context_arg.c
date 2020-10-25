#include "events/context_arg.h"
#include "main.h"
extern aconf *ac;

context_arg *carg_copy(context_arg *src)
{
	size_t carg_size = sizeof(context_arg);
	context_arg *carg = malloc(carg_size);
	memcpy(carg, src, carg_size);
	//printf("carg_size is %zu\n", carg_size);
	return carg;
}

void carg_free(context_arg *carg)
{
	if (carg->dest)
		free(carg->dest);

	if (carg->mesg)
		free(carg->mesg);

	if (carg->key)
		free(carg->key);

	if (carg->host)
		free(carg->host);


	if (carg->parser_name)
		free(carg->parser_name);

	if (carg->remote)
        	free(carg->remote);

	if (carg->local)
        	free(carg->local);

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

context_arg* context_arg_json_fill(json_t *root, host_aggregator_info *hi, void *handler, char *parser_name, char *mesg, size_t mesg_len, void *data, void *expect_function, uint8_t headers_pass, uv_loop_t *loop)
{
	context_arg *carg = calloc(1, sizeof(*carg));
	carg->ttl = -1;
	if (ac->log_level > 2)
		printf("allocated context argument with addr %p with hostname '%s' with mesg '%s'\n", carg, carg->host, carg->mesg);

	aconf_mesg_set(carg, mesg, mesg_len);

	carg->headers_pass = headers_pass;
	carg->url = hi->url;
	carg->hostname = hi->host; // old scheme
	strlcpy(carg->host, hi->host, 1024); // new scheme
	strlcpy(carg->port, hi->port, 6);
	if (carg->timeout < 1)
		carg->timeout = 5000;
	carg->proto = hi->proto;
	carg->transport = hi->transport;
	carg->tls = hi->tls;
	carg->loop = loop;
	carg->parser_handler = handler;
	carg->parser_name = parser_name;
	//*(carg->buffer) = uv_buf_init(carg->mesg, carg->mesg_len);
	carg->buflen = 1;
	carg->expect_function = expect_function;
	carg->data = data;
	carg->tt_timer = malloc(sizeof(uv_timer_t));
	carg->write = 2;
	carg->curr_ttl = -1;
	carg->buffer_request_size = 6553500;
	carg->buffer_response_size = 6553500;
	carg->full_body = string_init(6553500);
	if (hi->proto == APROTO_HTTPS || hi->proto == APROTO_TLS)
		carg->tls = 1;
	else
		carg->tls = 0;

	json_t *json_name = json_object_get(root, "name");
	char *str_name = (char*)json_string_value(json_name);
	if (str_name)
		carg->name = strdup(str_name);

	json_t *json_cert = json_object_get(root, "tls_certificate");
	char *str_cert = (char*)json_string_value(json_cert);
	if (str_cert)
		carg->tls_cert_file = strdup(str_cert);

	json_t *json_key = json_object_get(root, "tls_key");
	char *str_key = (char*)json_string_value(json_key);
	if (str_key)
		carg->tls_key_file = strdup(str_key);

	json_t *json_ca = json_object_get(root, "tls_ca");
	char *str_ca = (char*)json_string_value(json_ca);
	if (str_ca)
		carg->tls_ca_file = strdup(str_ca);

	json_t *json_timeout = json_object_get(root, "timeout");
	int64_t int_timeout = json_integer_value(json_timeout);
	if (json_timeout)
		carg->timeout = int_timeout;

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

	return carg;
}
