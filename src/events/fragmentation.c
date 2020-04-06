#include <stdio.h>
#include <stdint.h>
#include "context_arg.h"
void chunk_calc(context_arg* carg, char *buf, size_t nread)
{
	puts("CHUNK CALC");
	if (carg->chunked_size > nread)
	{
		printf("MATCH: 1: %lld > %lld\n", carg->chunked_size, nread);
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
			printf("MATCH2 buf+%lld/%lld\n: %s", n, nread, buf+n);
			string_cat(carg->full_body, buf+n, m);

			//char del[100];
			//snprintf(del, 100, "/%c.%lld", *buf+n, nread);
			//FILE *fd = fopen(del, "w");
			//printf("2 SAVED: %s\n", del);
			//fwrite(buf+n, m, 1, fd);
			//fclose(fd);

			n += m;

			carg->chunked_size = strtoll(buf+n, &tmp, 16);
			printf("chunked_size %lld\n", carg->chunked_size);
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
	puts("check for content size");
	printf("CHECK CHUNK HTTP VALIDATOR: %d && !%d", carg->chunked_expect, carg->chunked_size);
	if (carg->expect_body_length && carg->expect_body_length <= carg->full_body->l)
	{
		puts("content size match 1");
		return 1;
	}

	// check chunk http validator
	else if (carg->chunked_expect && !carg->chunked_size)
	{
		puts("chunked size 1");
		return 1;
	}

	// check expect function validator
	else if (carg->expect_function)
		return carg->expect_function(buf);

	else if (carg->expect_count <= carg->read_count)
	{
		puts("expect count 1");
		return 1;
	}

	return 0;

	//uint64_t full_body_size;
        //int8_t expect_json;
        //uint64_t expect_body_length;
        //int8_t expect_chunk;
}
