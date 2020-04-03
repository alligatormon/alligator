#include <stdio.h>
#include <inttypes.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "events/context_arg.h"
void json_handler(char *metrics, size_t size, context_arg *carg)
{
	json_parser_entry(metrics, 0, NULL, "json", carg);
	puts("==============");
	puts(metrics);
	puts("==============");
	//write_to_file("1", metrics, size, NULL, NULL);
	//FILE *fd = fopen("1", "w");
	//fwrite(metrics, strlen(metrics), 1, fd);
	//fclose(fd);
}
