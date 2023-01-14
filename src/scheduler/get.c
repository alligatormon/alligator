#include "scheduler/type.h"
#include "main.h"

int scheduler_compare(const void *a1, const void *a2)
{
	scheduler_node *s1 = (scheduler_node*)a1;
	scheduler_node *s2 = (scheduler_node*)a2;

	return strcmp(s1->name, s2->name);
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
