#include <stdio.h>
#include <stdint.h>
#include "context_arg.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

int64_t chunk_calc(context_arg* carg, char *buf, ssize_t nread, uint8_t copying, uint64_t *decoded_size)
{
	size_t bufsz = nread;
	carglog(carg, L_TRACE, "chunk_calc(%s): nread %zd preview %.*s\n", carg->key, nread, (int)(nread > 80 ? 80 : nread), buf);
	int ret = phr_decode_chunked(&carg->chunked_dec, buf, &bufsz);
	carglog(carg, L_TRACE, "phr_decode_chunked(%s): ret %d nread %zu decoded %zu\n", carg->key, ret, (size_t)nread, bufsz);
	if (decoded_size)
		*decoded_size = bufsz;
	if (bufsz)
	{
		carglog(carg, L_TRACE, "chunk decoded(%s): size %zu preview %.*s\n", carg->key, bufsz, (int)(bufsz > 80 ? 80 : bufsz), buf);
		if (copying)
			string_cat(carg->full_body, buf, bufsz);
		carglog(carg, L_TRACE, "chunk body(%s): total %zu preview %.*s\n", carg->key, carg->full_body->l, (int)(carg->full_body->l > 80 ? 80 : carg->full_body->l), carg->full_body->s);
	}

	return ret;
}

int8_t tcp_check_full(context_arg* carg, char *buf, size_t nread, int64_t chunk_ret)
{
	
	carglog(carg, L_TRACE, "check body full: length (%"u64"/%zu), chunk (X/%"d64"), count (%"PRIu8"/%"PRIu8"), function: %p\n", carg->full_body->l - carg->headers_size, carg->expect_body_length, carg->chunked_expect, carg->read_count, carg->expect_count, carg->expect_function);

	carglog(carg, L_TRACE, "expect %"d64": %"d64"\n", carg->chunked_expect, chunk_ret);
	if (carg->expect_body_length && carg->expect_body_length <= carg->full_body->l - carg->headers_size)
	{
		carglog(carg, L_TRACE, "check body full: length match\n");
		return 1;
	}

	//// check chunk http validator
	else if (carg->chunked_expect && chunk_ret >= 0)
	{
		carglog(carg, L_TRACE, "check body full: chunk match\n");
		return 1;
	}
	else if (carg->chunked_expect && chunk_ret == -1)
	{
		carglog(carg, L_TRACE, "check body full: chunk decode error, stop waiting\n");
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
