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

void dynatrace_response_catch(char *metrics, size_t size, context_arg *carg)
{
	puts("metrics start");
	puts(metrics);
	puts("metrics end");
}
