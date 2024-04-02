#include "reject.h"
#include <stdio.h>
#include <string.h>
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

void reject_insert(alligator_ht *reject_hash, char *label_name, char *label_value)
{
	uint32_t name_hash = tommy_strhash_u32(0, label_name);
	reject_t *reject = alligator_ht_search(reject_hash, reject_hash_compare, label_name, name_hash);
	if (!reject)
	{
		reject = calloc(1, sizeof(*reject));
		reject->name = label_name;

		if (label_value)
		{
			reject->ritem = calloc(1, sizeof(*reject->ritem));
			alligator_ht_init(reject->ritem);
		}

		alligator_ht_insert(reject_hash, &(reject->node), reject, name_hash);
	}

	if (!label_value)
		return;

	alligator_ht *ritem_hash = reject->ritem;
	uint32_t value_hash = tommy_strhash_u32(0, label_value);
	reject_item *ritem = alligator_ht_search(ritem_hash, reject_item_hash_compare, label_value, value_hash);
	if (!ritem)
	{
		ritem = calloc(1, sizeof(*ritem));
		ritem->value = label_value;
		
		alligator_ht_insert(ritem_hash, &(ritem->node), ritem, value_hash);
	}
}

void reject_delete(alligator_ht *reject_hash, char *label_name)
{
	uint32_t name_hash = tommy_strhash_u32(0, label_name);
	alligator_ht_remove(reject_hash, reject_hash_compare, label_name, name_hash);
}

int reject_metric(alligator_ht *reject_hash, char *label_name, char *label_value)
{
	if (!reject_hash)
		return 0;

	uint32_t name_hash = tommy_strhash_u32(0, label_name);
	reject_t *reject = alligator_ht_search(reject_hash, reject_hash_compare, label_name, name_hash);
	//printf("name %s: %p\n", label_name, reject);
	if (!reject)
		return 0;

	alligator_ht *ritem_hash = reject->ritem;
	if (!ritem_hash)
	{
		return 1;
	}

	uint32_t value_hash = tommy_strhash_u32(0, label_value);
	reject_item *ritem = alligator_ht_search(ritem_hash, reject_item_hash_compare, label_value, value_hash);
	//printf("value %s: %p\n", label_value, ritem);
	if (!ritem)
		return 0;

	return 1;
}
