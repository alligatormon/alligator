#include "scheduler/type.h"
#include "main.h"

int scheduler_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((scheduler_node*)obj)->name;

	return strcmp(s1, s2);
}

scheduler_node* scheduler_get(char *name)
{
	if (!name)
		return NULL;

	scheduler_node *sn = alligator_ht_search(ac->scheduler, scheduler_compare, name, tommy_strhash_u32(0, name));
	if (sn)
		return sn;
	else
		return NULL;
}
