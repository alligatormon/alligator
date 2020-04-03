#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/selector.h"
#include "common/url.h"
#include "main.h"
#include "context.h"
#include "common/base64.h"
#include "common/http.h"
#include "common/url.h"
#define CONF_MAXLENSIZE 65535

int context_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((config_context*)obj)->key;
	//printf("compare %s and %s\n", s1, s2);
	return strcmp(s1, s2);
}


// config_is_quotas return:
// 0 if no quotas
// 1 if quotas starts
// 2 if quotas continue
// 3 if quotas end
int8_t config_is_quotas(char *str, size_t len, char *quotas)
{
	for (int64_t i = 0; i<len; i++)
	{
		if ((str[i] == '\'') || (str[i] == '"'))
		{
			if (*quotas)
				return 3;
			else
				return 1;
		}
	}
	if (*quotas)
		return 2;
	else
		return 0;
}

mtlen* split_char_to_mtlen(char *str)
{
	if (!str)
		return NULL;

	mtlen *mt = malloc(sizeof(*mt));
	int64_t i, j, k;
	char *quotas = malloc(1000);
	*quotas = 0;
	size_t len = strlen(str);
	//printf("buf %s with size %zu\n", str, len);
	for (i=0, j=0, k=0; i<len; i++)
	{
		if ( isspace(str[i]) )
			continue;

		k = strcspn(str+i, ";{} \r\n\t\0");
		if (k)
			++j;
		int64_t z = i+k;
		if ( str[z] == ';' || str[z] == '{' || str[z] == '}' )
			++j;

		i += k;
	}
	mt->st = calloc(1, sizeof(stlen)*j);
	mt->max = j;

	for (i=0, j=0, k=0; i<len; i++)
	{
		if ( isspace(str[i]) )
			continue;

		int8_t qts = 0;
		k = strcspn(str+i, ";{} \r\n\t\0");
		if (k)
		{
			qts = config_is_quotas(str+i, k+1, quotas);
			//printf("qts is %d\n", qts);
			if (qts == 1)
				strncat(quotas, str+i+1, k+1);
			else if (qts == 2)
				strncat(quotas, str+i, k+1);
			else if (qts == 3)
			{
				strncat(quotas, str+i, k-1);
				mt->st[j].s = strdup(quotas);
				mt->st[j].l = strlen(quotas);
				*quotas = 0;
				//printf("%d: quotas copy '%s' with len %d\n", j, mt->st[j].s, mt->st[j].l);
				++j;
			}
			else
			{
				mt->st[j].s = strndup(str+i, k);
				mt->st[j].l = k;
				//printf("%d: copy '%s' with len %d\n", j, mt->st[j].s, mt->st[j].l);
				++j;
			}
		}
		int64_t z = i+k;
		if (!qts && (str[z] == ';' || str[z] == '{' || str[z] == '}'))
		{
			mt->st[j].s = strndup(str+z, 1);
			mt->st[j].l = 1;
			++j;
		}

		i += k;
	}

	mt->m = j;
	return mt;
}

void parse_config(mtlen *mt)
{
	extern aconf *ac;
	int64_t i;
	int splitflag = 0;
	char *ttmp;
	for (i=0;i<mt->m;i++)
	{
		if (ac->log_level > 2)
			printf("%"d64"/%zu: %s\n", i, mt->m, mt->st[i].s);
		if (!strncmp(mt->st[i].s, "exec://", 7))
		{
			splitflag = 1;
			char *tmp = strdup(mt->st[i].s);
			free(mt->st[i].s);
			mt->st[i].s = malloc(CONF_MAXLENSIZE);
			strcat(mt->st[i].s, tmp);
			ttmp = mt->st[i].s;
		}
		else if(splitflag && mt->st[i].s[0] != ';')
		{
			strcat(ttmp, " ");
			strcat(ttmp, mt->st[i].s);
			free(mt->st[i].s);
			mt->st[i].l = 0;
		}
		else if(splitflag && mt->st[i].s[0] == ';')
		{
			splitflag = 0;
			ttmp = 0;
		}
	}

	for (i=0;i<mt->m;i++)
	{
		config_context *ctx = tommy_hashdyn_search(ac->config_ctx, context_compare, mt->st[i].s, tommy_strhash_u32(0, mt->st[i].s));
		if (ctx)
			ctx->handler(mt, &i);
	}
}

int split_config(char *file)
{
	FILE *fd = fopen(file, "r");
	if (!fd)
		return 0;

	fseek(fd, 0, SEEK_END);
	int64_t fdsize = ftell(fd);
	rewind(fd);

#ifdef _WIN64
	int32_t psize = 10000;
#else
	int32_t psize = 0;
#endif

	char *buf = malloc(fdsize+psize);
	size_t rc = fread(buf, 1, fdsize, fd);
#ifndef _WIN64
	if (rc != fdsize)
	{
		fprintf(stderr, "I/O err read %s\n", file);
		return 0;
	}
#endif
	buf[rc-1] = 0;
	mtlen *mt = split_char_to_mtlen(buf);
	//int64_t i;
	//for (i=0;i<mt->m;i++)
	//{
	//	printf("%"d64": %s\n", i, mt->st[i].s);
	//}
	parse_config(mt);

	fclose(fd);

	return 1;
}
