#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "main.h"

void env_struct_gen_http_headers(void *funcarg, void* arg)
{
	env_struct *es = arg;
	string *stemplate = funcarg;

	string_cat(stemplate, es->k, strlen(es->k));
	string_cat(stemplate, ": ", 2);
	string_cat(stemplate, es->v, strlen(es->v));
	string_cat(stemplate, "\r\n", 2);
}

char* gen_http_query(int http_type, char *method_query, char *append_query, char *host, char *useragent, char *auth, int clrf, char *httpver, void *env_arg, void *proxy_settings)
{
	//printf("%d, %s, %s, %s, %s, %s, %d\n", http_type, method_query, append_query, host, useragent, auth, clrf);

	size_t method_query_size = 0;
	size_t append_query_size = 0;
	if (method_query)
		method_query_size = strlen(method_query);
	if (append_query)
		append_query_size = strlen(append_query);

	char *query = malloc(method_query_size + append_query_size + 2);
	*query = 0;
	if (method_query && method_query_size > 1)
		strcat(query, method_query);
	if (append_query)
		strcat(query, append_query);

	size_t size_query = strlen(query);
	if ( !size_query )
	{
		strlcpy(query, "/", 2);
	}


	char method[5];
	switch (http_type)
	{
		default: strlcpy(method, "GET", 4);
	}

	char *version_http = httpver ? httpver : "1.0";

	// new template
	tommy_hashdyn *env = env_arg;
	string *sret = string_init(method_query_size + append_query_size);
	string_cat(sret, method, strlen(method));
	string_cat(sret, " ", 1);
	string_cat(sret, query, strlen(query));
	string_cat(sret, " ", 1);
	string_cat(sret, "HTTP/", 5);
	string_cat(sret, version_http, strlen(version_http));
	string_cat(sret, "\r\n", 2);

	env_struct_push_alloc(env, "User-Agent", useragent);
	env_struct_push_alloc(env, "Host", host);
	if (auth)
	{
		char auth_basic_str[255];
		snprintf(auth_basic_str, 254, "Basic %s", auth);
		env_struct_push_alloc(env, "Authorization", auth_basic_str);
	}

	tommy_hashdyn_foreach_arg(env, env_struct_gen_http_headers, sret);

	string_cat(sret, "\r\n", 2);
	//puts(sret->s);
	// end
	char *buf = sret->s;
	free(sret);
	free(query);

	return buf;
}
