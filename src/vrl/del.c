#include "vrl/type.h"
#include "common/logs.h"
#include "main.h"
#include <string.h>

extern aconf *ac;

int vrl_del(char *name)
{
	if (!ac || !ac->vrl || !name)
		return 0;

	vrl_node *vn = alligator_ht_search(ac->vrl, vrl_node_compare, name,
					   tommy_strhash_u32(0, name));
	if (!vn) {
		glog(L_ERROR, "vrl '%s' not found\n", name);
		return 0;
	}

	alligator_ht_remove_existing(ac->vrl, &(vn->node));
	vrl_node_free(vn);
	return 1;
}
