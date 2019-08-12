#include <stdio.h>
#include "metric/namespace.h"
#include "events/client_info.h"
void dummy_handler(char *metrics, size_t size, client_info *cinfo)
{
	puts("DUMMY");
	printf("====================(%zu)================\n%s\n======================================\n", size, metrics);
}
