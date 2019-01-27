#include <stdio.h>
#include <string.h>
void redis_handler(char *metrics, size_t size, char *instance, int kind)
{
	printf("metrics is '%s\n", metrics);
	selector_get_plain_metrics(metrics, strlen(metrics), "\n", ":", "redis_", 6 );
}
