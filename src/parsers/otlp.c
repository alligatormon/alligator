#include <stdio.h>
#include <inttypes.h>
#include <jansson.h>
#include "common/json_query.h"
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "common/logs.h"
#include "common/validator.h"
#include "main.h"

void otlp_response_catch(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		carglog(carg, L_ERROR, "otlp_response_catch: json error on line %d: %s\n", error.line, error.text);
		return;
	}

	json_t *jpartialSuccess = json_object_get(root, "partialSuccess");
	if (jpartialSuccess) {
		carg->parser_status = 1;
	}
	else {
		carglog(carg, L_ERROR, "otlp_response_catch: wrong answer: '%s'\n", metrics);
	}
}
