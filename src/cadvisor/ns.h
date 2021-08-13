#pragma once
#include "cadvisor/metrics.h"
char* get_ifname_by_cgroup_id(char *slice, char *cntid, alligator_ht *ifhash, char *name);
