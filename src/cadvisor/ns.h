#pragma once
#include "cadvisor/metrics.h"
char* get_ifname_by_cgroup_id(char *slice, char *cntid, alligator_ht *ifhash, char *name);
int net_ns_mount(int fd, char *name);
int mount_ns_by_cgroup_procs(char *dirpath, char *name);
int mount_ns_by_pid(char *pid);
