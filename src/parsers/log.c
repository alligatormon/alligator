#include <stdio.h>
#include <string.h>
#include "common/selector.h"
#include "common/logs.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/pcre_parser.h"

void log_handler(char *metrics, size_t size, context_arg *carg)
{
	carglog(carg, L_INFO, "log_handler(%zu):'%s'\n", size, metrics);

	if (carg && carg->rematch && metrics)
	{
		char *line = malloc(size + 1);
		if (line)
		{
			memcpy(line, metrics, size);
			line[size] = 0;

			for (size_t i = 0; carg->rematch[i]; ++i)
				pcre_match(carg->rematch[i], line);

			free(line);
		}
	}

	carg->parser_status = 1;
}
