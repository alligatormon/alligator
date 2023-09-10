#include "system/fdescriptors.h"
#include <inttypes.h>

int process_fdescriptors_compare(const void* arg, const void* obj)
{
	uint32_t s1 = *(uint32_t*)arg;
	uint32_t s2 = ((process_fdescriptors*)obj)->fd;
	return s1 != s2;
}


void process_fdescriptors_free(void *funcarg, void* arg)
{
	process_fdescriptors *fdescriptors = arg;
	if (!fdescriptors)
		return;

	if (fdescriptors->procname)
	{
		free(fdescriptors->procname);
		fdescriptors->procname = NULL;
	}

	free(fdescriptors);
}

