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
