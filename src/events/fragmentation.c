#include <stdio.h>
#include <stdint.h>
#include "context_arg.h"
#include "main.h"
extern aconf *ac;

void chunk_calc(context_arg* carg, char *buf, size_t nread)
{
	//puts("CHUNK CALC");
	if (carg->chunked_size > nread)
	{
		//printf("MATCH: 1: %lld > %lld\n", carg->chunked_size, nread);
		string_cat(carg->full_body, buf, nread);
		carg->chunked_size -= nread;

			//char del[100];
			//snprintf(del, 100, "/%c.%lld", *buf, nread);
			//FILE *fd = fopen(del, "w");
			//printf("1 SAVED: %s\n", del);
			//fwrite(buf, nread, 1, fd);
			//fclose(fd);
	}
	else
	{
		char *tmp;
		uint64_t n = 0;
		uint64_t m = 0;
		while (carg->chunked_size && n<nread)
		{
			m = strcspn(buf+n, "\r\n");
			//printf("MATCH2 buf+%lld/%lld\n: %s", n, nread, buf+n);
			string_cat(carg->full_body, buf+n, m);

			//char del[100];
			//snprintf(del, 100, "/%c.%lld", *buf+n, nread);
			//FILE *fd = fopen(del, "w");
			//printf("2 SAVED: %s\n", del);
			//fwrite(buf+n, m, 1, fd);
			//fclose(fd);

			n += m;

			carg->chunked_size = strtoll(buf+n, &tmp, 16);
			//printf("chunked_size %lld\n", carg->chunked_size);
			n = tmp - buf;
			n += strcspn(buf+n, "\r\n");
			n++;
		}
	}
}

int8_t tcp_check_full(context_arg* carg, char *buf, size_t nread)
{
	//printf("check buf for '%s'\n", buf);
	// check body size
	if (ac->log_level > 2)
		printf("check body full: length (%"u64"/%zu), chunk (%"u64"/%zu), count (%"PRIu8"/%"PRIu8"), function: %p\n", carg->full_body->l, carg->expect_body_length, carg->chunked_size, carg->chunked_expect, carg->read_count, carg->expect_count, carg->expect_function);

	if (carg->expect_body_length && carg->expect_body_length <= carg->full_body->l)
	{
		if (ac->log_level > 2)
			puts("check body full: length match");
		return 1;
	}

	// check chunk http validator
	else if (carg->chunked_expect && !carg->chunked_size)
	{
		if (ac->log_level > 2)
			puts("check body full: chunk match");
		return 1;
	}

	// check expect function validator
	else if (carg->expect_function)
	{
		if (ac->log_level > 2)
			puts("check body full: function match");
		return carg->expect_function(buf);
	}

	else if (carg->expect_count && (carg->expect_count <= carg->read_count))
	{
		if (ac->log_level > 2)
			puts("check body full: max retry match");
		return 1;
	}

	if (ac->log_level > 2)
		puts("check body full: no match, read more");
	return 0;
}
