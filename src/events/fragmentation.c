#include <stdio.h>
#include <stdint.h>
#include "context_arg.h"
#include "main.h"
extern aconf *ac;

void chunk_calc(context_arg* carg, char *buf, ssize_t nread)
{
	char *tmp = buf;
	ssize_t tsize = nread;
	uint64_t offset = 0;
	//printf("CHUNK CALC %"d64", %"d64"\n", carg->chunked_size, carg->chunked_expect);
	if (carg->chunked_size < 0)
	{
		//puts("case -1");
		carg->chunked_done = 1;
		return;
	}
	if (!carg->chunked_expect)
		return;
	if (!buf)
		return;

	if (!carg->chunked_size)
	{
		//puts("case 0");
		offset = strspn(tmp, "\r\n");
		tsize = nread - offset;
		tmp += offset;

		carg->chunked_size = strtoll(tmp, NULL, 16);

		offset = strcspn(tmp, "\r\n");
		offset += strspn(tmp+offset, "\r\n");
		tsize = nread - (tmp - buf) - offset;
		tmp += offset;
	}

	if (tsize < carg->chunked_size)
	{
		//printf("case 1: cat %"d64"\n", tsize);
		string_cat(carg->full_body, tmp, tsize);
		carg->chunked_size -= tsize;
	}
	else
	{
		//puts("case 2");
		size_t n;
		for (n = 0; n < tsize; n++)
		{
			//printf("loop case 3: read %"d64"\n", carg->chunked_size);
			string_cat(carg->full_body, tmp+n, carg->chunked_size);
			n += carg->chunked_size;
			n += strspn(tmp, "\r\n");
			
			//printf("strtoll '%s'\n", tmp);
			carg->chunked_size = strtoll(tmp, NULL, 16);
			if (!carg->chunked_size)
			{
				//puts("loop end 3");
				carg->chunked_done = 1;
				break;
			}
		}
	}
}

int8_t tcp_check_full(context_arg* carg, char *buf, size_t nread)
{
	//printf("check buf for '%s'\n", buf);
	// check body size
	if (ac->log_level > 2)
		printf("check body full: length (%"u64"/%zu), chunk (%"d64"/%"d64"), count (%"PRIu8"/%"PRIu8"), function: %p\n", carg->full_body->l - carg->headers_size, carg->expect_body_length, carg->chunked_size, carg->chunked_expect, carg->read_count, carg->expect_count, carg->expect_function);

	if (carg->expect_body_length && carg->expect_body_length <= carg->full_body->l - carg->headers_size)
	{
		if (ac->log_level > 2)
			puts("check body full: length match");
		return 1;
	}

	// check chunk http validator
	else if (carg->chunked_expect && carg->chunked_done)
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
		return carg->expect_function(buf, nread);
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
