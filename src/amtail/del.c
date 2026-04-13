#include "amtail/type.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

int amtail_del(char *name) {
    if (!ac || !ac->amtail || !name)
        return 0;

    amtail_node *an = alligator_ht_search(ac->amtail, amtail_node_compare, name, tommy_strhash_u32(0, name));
    if (!an) {
        glog(L_ERROR, "amtail '%s' not found\n", name);
        return 0;
    }

    alligator_ht_remove_existing(ac->amtail, &(an->node));
    amtail_node_free(an);
    return 1;
}