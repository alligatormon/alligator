#pragma once
#include "dstructures/tommy.h"
#ifdef __linux__
#include "system/linux/network.h"
#endif
#include "dstructures/ht.h"
#include "events/context_arg.h"

void cadvisor_register_metric_families(context_arg *carg);
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
#ifdef __linux__
typedef struct cadvisor_net_emit_ctx {
	char *cntid;
	char *name;
	char *image;
	char *cad_id;
	char *kubenamespace;
	char *kubepod;
	char *kubecontainer;
	char *libvirt_id;
} cadvisor_net_emit_ctx;

int cadvisor_netlink_emit_cb(const network_if_stats *st, void *arg);
void cgroup_emit_netinfo_stats(const network_if_stats *st, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id);
#endif
#ifndef CGROUP2_SUPER_MAGIC
#define CGROUP2_SUPER_MAGIC     0x63677270
#endif
