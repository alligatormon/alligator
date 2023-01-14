#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "main.h"

void lang_parser_handler(char *metrics, size_t size, context_arg *carg)
{
	//if (carg->log_level > 3)
	if (1)
	{
		puts("====lang_parser_handler===============");
		printf("'%s'\n", metrics);
		printf("carg->lang '%s'\n", carg->lang);
	}

	string *request = string_init_add_auto(metrics);
	lang_run(carg->lang, NULL, request, carg->response);

	printf("response is '%s'\n", carg->response->s);
}
