#include "main.h"
#include "dstructures/ht.h"
#include "action/type.h"

int action_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((action_node*)obj)->name;
	return strcmp(s1, s2);
}

action_node* action_get(char *name)
{
	action_node *an = alligator_ht_search(ac->action, action_compare, name, tommy_strhash_u32(0, name));
	if (an)
		return an;
	else
		return NULL;
}

