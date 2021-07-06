#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "common/http.h"
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

char* gen_http_query(int http_type, char *method_query, char *append_query, char *host, char *useragent, char *auth, int clrf, char *httpver, void *env_arg, void *proxy_settings, string *body)
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
	else if (method_query && method_query_size == 1 && append_query && *append_query != '/')
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
		case HTTP_POST: strlcpy(method, "POST", 5);
			 break;
		default: strlcpy(method, "GET", 4);
			 break;
	}

	char *version_http = httpver ? httpver : "1.0";

	// new template
	tommy_hashdyn *env = env_arg;
	if (!env)
	{
		env = malloc(sizeof(tommy_hashdyn));
		tommy_hashdyn_init(env);
	}
	string *sret = string_init(method_query_size + append_query_size);
	string_cat(sret, method, strlen(method));
	string_cat(sret, " ", 1);
	string_cat(sret, query, strlen(query));
	string_cat(sret, " ", 1);
	string_cat(sret, "HTTP/", 5);
	string_cat(sret, version_http, strlen(version_http));
	string_cat(sret, "\r\n", 2);

	env_struct_push_alloc(env, "User-Agent", useragent);
	if (*host == '/')
		env_struct_push_alloc(env, "Host", "localhost");
	else
		env_struct_push_alloc(env, "Host", host);

	if (auth)
	{
		char auth_basic_str[255];
		snprintf(auth_basic_str, 254, "Basic %s", auth);
		env_struct_push_alloc(env, "Authorization", auth_basic_str);
	}

	tommy_hashdyn_foreach_arg(env, env_struct_gen_http_headers, sret);

	string_cat(sret, "\r\n", 2);

	if (body)
		string_string_cat(sret, body);

	//puts(sret->s);
	// end
	char *buf = sret->s;
	free(sret);
	free(query);

	return buf;
}

uint64_t urlencode(char* dest, char* src, size_t src_len)
{
  uint64_t i;
  uint64_t ret = 0;
  *dest = 0;
  for(i = 0; i < src_len; i++, ret++) {
    char c = src[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      char t[2];
      t[0] = c; t[1] = '\0';
      strcat(dest, t);
    } else {
      char t[4];
      snprintf(t, sizeof(t), "%%%02x", c & 0xff);
      strcat(dest, t);
      ret += 2;
    }
  }
  return ret;
}

uint64_t urldecode(char* dest, char *src, size_t len) {
    char ch;
    int ii;
    uint64_t j = 0;

    char substr[3];

    for (uint64_t i=0; i < len; i++) {
        if (src[i] != '%') {
            if(src[i] == '+')
            {
                dest[j++] = ' ';
            }
            else
            {
                dest[j++] = src[i];
            }
        } else {
            strlcpy(substr, src + i + 1, 3);
            sscanf(substr, "%x", &ii);
            ch = (char)ii;
            dest[j++] = ch;
            i = i + 2;
        }
    }
    dest[j--] = 0;
    return j;
}

int http_arg_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((http_arg*)obj)->key;
	return strcmp(s1, s2);
}

tommy_hashdyn* http_get_args(char *str, size_t size)
{
	tommy_hashdyn *args = malloc(sizeof(tommy_hashdyn));
	tommy_hashdyn_init(args);

	if (!str)
		return args;

	char *start = strstr(str, "?");
	if (!start)
		return args;

	++start;

	uint64_t args_size = size - (start - str);
	char token[args_size];
	char decodeurl[args_size + 1024];

	for (uint64_t i = 0; i < args_size; i++)
	{
	        uint64_t token_size = str_get_next(start, token, args_size + 1, "&", &i);
		//printf("token '%s'/%"u64" from '%s'\n", token, token_size, start);

		uint64_t j = 0;
		char key[token_size];
		char value[token_size];

	        uint64_t key_size = str_get_next(token, key, token_size, "=", &j);

		uint32_t key_hash = tommy_strhash_u32(0, key);
		if (!tommy_hashdyn_search(args, http_arg_compare, key, key_hash))
		{
			http_arg *ha = malloc(sizeof(*ha));
			ha->key = strdup(key);

			strlcpy(value, token + key_size + 1, token_size - key_size + 1);
			//urldecode(decodeurl, value, args_size);
			urldecode(decodeurl, value, token_size);
			//printf("decoded is '%s'\n", decodeurl);
			ha->value = strdup(decodeurl);

			tommy_hashdyn_insert(args, &(ha->node), ha, key_hash);
			//printf("key '%s', value '%s'\n", ha->key, ha->value);
		}
	}

	return args;
}

void http_args_free_for(void *funcarg, void* arg)
{
	http_arg *harg = arg;
	if (!harg)
		return;

	free(harg->key);
	free(harg->value);
	free(harg);
}

void http_args_free(tommy_hashdyn *arg)
{
	tommy_hashdyn_foreach_arg(arg, http_args_free_for, NULL);
	tommy_hashdyn_done(arg);
	free(arg);
}
