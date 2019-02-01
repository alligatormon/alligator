#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/selector.h"
#include "file.h"
#include "main.h"
#include "context.h"
#include "common/base64.h"
#include "common/http.h"
#define CONF_MAXLENSIZE 65535

int context_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((config_context*)obj)->key;
	printf("compare %s and %s\n", s1, s2);
	return strcmp(s1, s2);
}

mtlen* split_char_to_mtlen(char *str)
{
	puts("1");
	mtlen *mt = malloc(sizeof(*mt));
	puts("2");
	int64_t i, j, k;
	puts("3");
	size_t len = strlen(str);
	printf("buf %s with size %zu\n", str, len);
	puts("4");
	for (i=0, j=0, k=0; i<len; i++)
	{
	puts("5");
		if ( isspace(str[i]) )
			continue;
	puts("6");

		k = strcspn(str+i, ";{} \r\n\t\0");
	puts("7");
		int64_t z = i+k;
	puts("8");
		if ( str[z] == ';' || str[z] == '{' || str[z] == '}' )
			j++;
	puts("9");

		i += k;
	puts("10");
		j++;
	puts("11");
	}
	puts("12");
	mt->st = malloc(sizeof(stlen)*j);
	puts("13");
	mt->m = j;
	puts("14");

	for (i=0, j=0, k=0; i<len; i++)
	{
	puts("15");
		if ( isspace(str[i]) )
			continue;
	puts("16");

		k = strcspn(str+i, ";{} \r\n\t\0");
	puts("17");
		mt->st[j].s = strndup(str+i, k);
	puts("18");
		mt->st[j].l = k;
	puts("19");
		int64_t z = i+k;
	puts("20");
		if ( str[z] == ';' || str[z] == '{' || str[z] == '}' )
		{
	puts("21");
			j++;
	puts("22");
			mt->st[j].s = strndup(str+z, 1);
	puts("23");
			mt->st[j].l = 1;
	puts("24");
		}
	puts("25");

		i += k;
	puts("26");
		j++;
	puts("27");
	}

	puts("28");
	return mt;
}

void parse_config(mtlen *mt)
{
	extern aconf *ac;
	int64_t i;
	for (i=0;i<mt->m;i++)
	{
		//printf("%"d64": %s %zu %p\n", i, mt->st[i].s, tommy_hashdyn_count(ac->config_ctx), ac->config_ctx);
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
	puts("mtlen config");
	mtlen *mt = split_char_to_mtlen(buf);
	puts("mtlen config end");
	//int64_t i;
	//for (i=0;i<mt->m;i++)
	//{
	//	printf("%"d64": %s\n", i, mt->st[i].s);
	//}
	puts("parse config");
	parse_config(mt);
	puts("parse config end");

	fclose(fd);

	return 1;
}

host_aggregator_info *parse_url (char *str, size_t len)
{
	host_aggregator_info *hi = malloc(sizeof(*hi));
	hi->auth = 0;
	char *tmp;
	printf("PARSE %s (%zu)\n", str, len);
	if ( !strncmp(str, "http://", 7) )
	{
		int64_t k, l, m;
		tmp = str+7;
		hi->proto = APROTO_HTTP;

		k = strcspn(tmp, ":");
		l = strcspn(tmp, "@");
		m = strcspn(tmp, "/");
		if ( k < l && l < m )
		{
			hi->proto = APROTO_HTTP_AUTH;
			//unsigned char *buf = strndup(tmp, l);
			size_t sz;
			hi->auth = base64_encode((unsigned char*)tmp, l, &sz);
			//free(buf);
			tmp += l +1;
			k = strcspn(tmp, ":");
			m = strcspn(tmp, "/");
			if ( k > m )
				k = m;
		}

		if ( tmp[k-1] == '/' )
			hi->host = strndup(tmp, k-1);
		else
			hi->host = strndup(tmp, k);

		tmp += k + 1;
		k = strcspn(tmp, "/");
		strlcpy(hi->port, tmp, k+1);
		if (!strlen(hi->port))
			strlcpy(hi->port, "80", 3);

		tmp += k;
		hi->query = strdup(tmp);
	}
	else if ( !strncmp(str, "tcp://", 6) )
	{
		int64_t k;
		tmp = str+6;
		hi->proto = APROTO_TCP;

		k = strcspn(tmp, ":");
		hi->host = strndup(tmp, k);

		tmp += k + 1;
		k = strcspn(tmp, "/");
		strlcpy(hi->port, tmp, k+1);
	}
	else if ( !strncmp(str, "udp://", 6) )
	{
		int64_t k;
		tmp = str+6;
		hi->proto = APROTO_UDP;

		k = strcspn(tmp, ":");
		hi->host = strndup(tmp, k);

		tmp += k + 1;
		k = strcspn(tmp, "/");
		strlcpy(hi->port, tmp, k+1);
	}
	else if ( !strncmp(str, "file://", 7) )
	{
		tmp = str+7;
		hi->proto = APROTO_FILE;
		hi->host = strdup(tmp);
	}
	else if ( !strncmp(str, "unix://", 7) )
	{
		tmp = str+7;
		hi->proto = APROTO_UNIX;
		hi->host = strdup(tmp);
	}
	else if ( !strncmp(str, "unixgram://", 11) )
	{
		tmp = str+11;
		hi->proto = APROTO_UNIXGRAM;
		hi->host = strdup(tmp);
	}

	return hi;
}
