#include "reject.h"
#include <stdio.h>
int reject_hash_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((reject_t*)obj)->name;
        return strcmp(s1, s2);
}

int reject_item_hash_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((reject_item*)obj)->value;
        return strcmp(s1, s2);
}

void reject_insert(tommy_hashdyn *reject_hash, char *label_name, char *label_value)
{
	uint32_t name_hash = tommy_strhash_u32(0, label_name);
	reject_t *reject = tommy_hashdyn_search(reject_hash, reject_hash_compare, label_name, name_hash);
	if (!reject)
	{
		reject = calloc(1, sizeof(*reject));
		reject->name = label_name;

		if (label_value)
		{
			reject->ritem = calloc(1, sizeof(*reject->ritem));
			tommy_hashdyn_init(reject->ritem);
		}

		tommy_hashdyn_insert(reject_hash, &(reject->node), reject, name_hash);
	}

	if (!label_value)
		return;

	tommy_hashdyn *ritem_hash = reject->ritem;
	uint32_t value_hash = tommy_strhash_u32(0, label_value);
	reject_item *ritem = tommy_hashdyn_search(ritem_hash, reject_item_hash_compare, label_value, value_hash);
	if (!ritem)
	{
		ritem = calloc(1, sizeof(*ritem));
		ritem->value = label_value;
		
		tommy_hashdyn_insert(ritem_hash, &(ritem->node), ritem, value_hash);
	}
}

int reject_metric(tommy_hashdyn *reject_hash, char *label_name, char *label_value)
{
	uint32_t name_hash = tommy_strhash_u32(0, label_name);
	reject_t *reject = tommy_hashdyn_search(reject_hash, reject_hash_compare, label_name, name_hash);
	//printf("name %s: %p\n", label_name, reject);
	if (!reject)
		return 0;

	tommy_hashdyn *ritem_hash = reject->ritem;
	if (!ritem_hash)
	{
		return 1;
	}

	uint32_t value_hash = tommy_strhash_u32(0, label_value);
	reject_item *ritem = tommy_hashdyn_search(ritem_hash, reject_item_hash_compare, label_value, value_hash);
	//printf("value %s: %p\n", label_value, ritem);
	if (!ritem)
		return 0;

	return 1;
}
