#include <stdio.h>
#include <stdint.h>
#include "context_arg.h"
#include "main.h"
extern aconf *ac;

uint64_t chunk_calc(context_arg* carg, char *buf, ssize_t nread, uint8_t copying)
{
	size_t prevsize;
	size_t bufsz = prevsize = nread;
	//printf("\n\n\n\n========bus is=====\n'%s'\n\t=====\n", buf);
	phr_decode_chunked(&carg->chunked_dec, buf, &bufsz);
	//printf("0phr_decode_chunked return: %zd/%zu: %zu, prevsize %zu\n", ret, nread, bufsz, prevsize);
	if (bufsz)
	{
		//printf("\n+++++ 1decoded: ++++\n'%s'\n++++end+++++\n", buf);
		//strlcpy(carg->full_body->s, buf, bufsz + 1);
		if (copying)
			string_cat(carg->full_body, buf, bufsz);
		//printf("\n+++++ 2decoded: ++++\n'%s'\n=======end=======\n\n\n\n", carg->full_body->s);
	}

	return bufsz;
}

int8_t tcp_check_full(context_arg* carg, char *buf, size_t nread, uint64_t chunksize)
{
	
	if (ac->log_level > 2)
		printf("check body full: length (%"u64"/%zu), chunk (X/%"d64"), count (%"PRIu8"/%"PRIu8"), function: %p\n", carg->full_body->l - carg->headers_size, carg->expect_body_length, carg->chunked_expect, carg->read_count, carg->expect_count, carg->expect_function);

	//printf("expect %"d64": %"u64"\n", carg->chunked_expect, chunksize);
	if (carg->expect_body_length && carg->expect_body_length <= carg->full_body->l - carg->headers_size)
	{
		if (ac->log_level > 2)
			puts("check body full: length match");
		return 1;
	}

	//// check chunk http validator
	else if (carg->chunked_expect && !chunksize)
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
