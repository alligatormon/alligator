#include "events/context_arg.h"

context_arg *carg_copy(context_arg *src)
{
	size_t carg_size = sizeof(context_arg);
	context_arg *carg = malloc(carg_size);
	memcpy(carg, src, carg_size);
	printf("carg_size is %zu\n", carg_size);
	return carg;
}

void carg_free(context_arg *carg)
{
	if (carg->dest)
		free(carg->dest);

	if (carg->mesg)
		free(carg->mesg);

	if (carg->key)
		free(carg->key);

	if (carg->hostname)
		free(carg->hostname);


	if (carg->parser_name)
		free(carg->parser_name);

	if (carg->remote)
        	free(carg->remote);

	if (carg->local)
        	free(carg->local);

	free(carg);
}
