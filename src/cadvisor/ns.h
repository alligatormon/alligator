#pragma once
#include "cadvisor/metrics.h"
char* get_ifname_by_cgroup_id(char *slice, char *cntid, tommy_hashdyn *ifhash, char *name);
