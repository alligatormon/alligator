#include <stdio.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
void dummy_handler(char *metrics, size_t size, context_arg *carg)
{
	puts("DUMMY");
	printf("====================(%zu)================\n%s\n======================================\n", size, metrics);
}
