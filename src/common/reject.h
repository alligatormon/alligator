#pragma once
#include "dstructures/tommy.h"

typedef struct reject_item
{
	char *value;
	tommy_node node;
} reject_item;

typedef struct reject_t
{
	char *name;
	alligator_ht *ritem;

	tommy_node node;
} reject_t;

int reject_hash_compare(const void* arg, const void* obj);
int reject_metric(alligator_ht *reject_hash, char *label_name, char *label_value);
void reject_delete(alligator_ht *reject_hash, char *label_name);
void reject_insert(alligator_ht *reject_hash, char *label_name, char *label_value);
