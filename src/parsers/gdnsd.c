#include <stdio.h>
#include <jansson.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
void gdnsd_handler(char *metrics, size_t size, context_arg *carg)
{
	json_parser_entry(metrics+8, 0, NULL, "gdnsd", carg);
}
int8_t gdnsd_validator(char *data, size_t size)
{
	json_error_t error;
	json_t *root = json_loads(data+8, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return 0;
	}
	return 1;
}
