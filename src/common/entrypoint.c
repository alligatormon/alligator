#include "common/entrypoint.h"
int entrypoint_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((context_arg*)obj)->key;
	return strcmp(s1, s2);
}
