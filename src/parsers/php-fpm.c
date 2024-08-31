#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "main.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/json_parser.h"
#include "common/aggregator.h"
typedef struct fcgi_reply_data
{
	int64_t content_length;
	char *body;
} fcgi_reply_data;

uv_buf_t* gen_fcgi_query(char *method_query, char *host, char *useragent, char *auth, size_t *num);
fcgi_reply_data* fcgi_reply_parser(char *fcgi, size_t n);

uv_buf_t* gen_fcgi_query(char *method_query, char *host, char *useragent, char *auth, size_t *num)
{
	*num=5;

	uint8_t filename_size = strcspn(method_query, "?");
	char* filename_query = strndup(method_query, filename_size);
	char* query_string = method_query+filename_size+1;

	uint16_t size_payload, size_method_query;
	size_method_query = strlen(method_query);
	uint8_t query_string_size = size_method_query-filename_size-1;
	size_payload = 84 + filename_size*2 + query_string_size;

	uint8_t offset = 8 - (size_payload%8);
	uint16_t total_size = size_payload+offset;
	uint8_t sz1 = (total_size - offset) >> 8;
	uint8_t sz2 = total_size - offset;

	uv_buf_t *buffer = malloc(sizeof(*buffer)*(*num));
	buffer[0] = uv_buf_init("\1\1\0\1\0\10\0\0", 8);
	buffer[1] = uv_buf_init("\0\1\0\0\0\0\0\0", 8);

	char *gen_headers = malloc(8);
	gen_headers[0] = '\1';
	gen_headers[1] = '\4';
	gen_headers[2] = '\0';
	gen_headers[3] = '\1';
	gen_headers[4] = sz1;
	gen_headers[5] = sz2;
	gen_headers[6] = offset;
	gen_headers[7] = '\0';
	buffer[2] = uv_buf_init(gen_headers, 8);

	char *gen_query = malloc(size_payload);
	uint8_t size_dir = 21 + filename_size;
	int16_t cur = 0;
	gen_query[cur++] = '\16';
	gen_query[cur++] = '\3';
	memcpy(gen_query+cur, "REQUEST_METHODGET", 17);
	cur+=17;
	gen_query[cur++] = '\17';
	//gen_query[cur++] = '\34';
	gen_query[cur++] = size_dir;
	memcpy(gen_query+cur, "SCRIPT_FILENAME/usr/share/nginx/html", 36);
	cur+=36;
	memcpy(gen_query+cur, filename_query, filename_size);
	cur+=filename_size;

	gen_query[cur++] = '\v';
	gen_query[cur++] = filename_size;
	memcpy(gen_query+cur, "SCRIPT_NAME", 11);
	cur+=11;
	memcpy(gen_query+cur, filename_query, filename_size);
	cur+=filename_size;

	gen_query[cur++] = 12;
	gen_query[cur++] = query_string_size;
	memcpy(gen_query+cur, "QUERY_STRING", 12);
	cur+=12;
	memcpy(gen_query+cur, query_string, query_string_size);
	cur+=query_string_size;

	for (; cur < total_size;)
		gen_query[++cur] = '\0';

	buffer[3] = uv_buf_init(gen_query, total_size);
	buffer[4] = uv_buf_init("\1\4\0\1\0\0\0\0", 8);

	return buffer;
}

fcgi_reply_data* fcgi_reply_parser(char *fcgi, size_t n)
{
	fcgi_reply_data *fcgi_reply = malloc(sizeof(*fcgi_reply));
	fcgi_reply->content_length = (fcgi[5] << 8) + fcgi[6];
	//printf("%"d64"\n", fcgi_reply->content_length);
	fcgi_reply->body = strstr(fcgi+8, "\r\n\r\n");
	return fcgi_reply;
}

void php_fpm_handler(char *metrics, size_t size, context_arg *carg)
{
	char **parsestring = malloc(sizeof(char*)*1);
	parsestring[0] = strdup("print::php_fpm_processes(pid)");
	json_parser_entry(metrics, 1, parsestring, "php_fpm", carg);
	free(parsestring[0]);
	free(parsestring);
	carg->parser_status = 1;
}

string* php_fpm_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	if ((hi->proto == APROTO_HTTP) || (hi->proto == APROTO_HTTPS))
		return string_init_add_auto("show sess\n");
	else
		return NULL;
}

void php_fpm_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("php-fpm");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = php_fpm_handler;
	actx->handler[0].validator = json_validator;
	actx->handler[0].mesg_func = php_fpm_mesg;
	strlcpy(actx->handler[0].key,"php-fpm", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
