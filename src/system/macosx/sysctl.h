#pragma once
#include "dstructures/ht.h"

typedef struct sysctl_node {
	char *name;
	tommy_node node;
} sysctl_node;

void sysctl_run(alligator_ht *sysctl);
void sysctl_free(alligator_ht *sysctl);
