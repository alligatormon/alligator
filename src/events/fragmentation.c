#include <stdio.h>
#include <stdint.h>
#include "context_arg.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

uint64_t chunk_calc(context_arg* carg, char *buf, ssize_t nread, uint8_t copying)
{
	size_t bufsz = nread;
	carglog(carg, L_TRACE, "\n\n\n\n========bus is=====\n'%s'\n\t=====\n", buf);
	int ret = phr_decode_chunked(&carg->chunked_dec, buf, &bufsz);
	carglog(carg, L_TRACE, "0phr_decode_chunked return: %d/%zu: %zu\n", ret, nread, bufsz);
	if (bufsz)
	{
		carglog(carg, L_TRACE, "\n+++++ 1decoded: ++++\n'%s'\n++++end+++++\n", buf);
		if (copying)
			string_cat(carg->full_body, buf, bufsz);
		carglog(carg, L_TRACE, "\n+++++ 2decoded: ++++\n'%s'\n=======end=======\n\n\n\n", carg->full_body->s);
	} else {
		if (nread > bufsz) {
			return nread;
		}
	}

	return bufsz;
}

int8_t tcp_check_full(context_arg* carg, char *buf, size_t nread, uint64_t chunksize)
{
	
	carglog(carg, L_TRACE, "check body full: length (%"u64"/%zu), chunk (X/%"d64"), count (%"PRIu8"/%"PRIu8"), function: %p\n", carg->full_body->l - carg->headers_size, carg->expect_body_length, carg->chunked_expect, carg->read_count, carg->expect_count, carg->expect_function);

	carglog(carg, L_TRACE, "expect %"d64": %"u64"\n", carg->chunked_expect, chunksize);
	if (carg->expect_body_length && carg->expect_body_length <= carg->full_body->l - carg->headers_size)
	{
		carglog(carg, L_TRACE, "check body full: length match\n");
		return 1;
	}

	//// check chunk http validator
	else if (carg->chunked_expect && !chunksize)
	{
		carglog(carg, L_TRACE, "check body full: chunk match\n");
		return 1;
	}

	// check expect function validator
	else if (carg->expect_function)
	{
		carglog(carg, L_TRACE, "check body full: function match\n");
		return carg->expect_function(carg, buf, nread);
	}

	else if (carg->expect_count && (carg->expect_count <= carg->read_count))
	{
		carglog(carg, L_TRACE, "check body full: max retry match\n");
		return 1;
	}

	carglog(carg, L_TRACE, "check body full: no match, read more\n");
	return 0;
}
