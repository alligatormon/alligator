#pragma once
#include "dstructures/tommy.h"
#include "dstructures/ht.h"
//typedef struct ifindexnames
//{
//	char *key;
//	char *value;
//	
//	tommy_node node;
//} ifindexnames;

//int ifindexnames_compare(const void* arg, const void* obj);
//alligator_ht* network_index_scrape();
void cadvisor_scrape(char *ifname, char *cgroupPath, char *slice, char *cntid, char *name, char *image, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id);
void cgroup_get_netinfo(char *sysfs, char *ifname, char *stat, char *mname, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id);
#ifndef CGROUP2_SUPER_MAGIC
#define CGROUP2_SUPER_MAGIC     0x63677270
#endif
