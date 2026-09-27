#pragma once
#include "dstructures/tommy.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __linux__
#include "system/linux/network.h"
#endif
#include "dstructures/ht.h"
#include "events/context_arg.h"

void cadvisor_register_metric_families(context_arg *carg);
void cadvisor_scrape(char *ifname, char *cgroupPath, char *slice, char *cntid, char *name, char *image, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id);

typedef struct cadvisor_label_pair {
	const char *name;
	const char *value;
} cadvisor_label_pair;

void add_cadvisor_metric_uint(char *mname, uint64_t val, char *cntid, char *name, char *image, char *cad_id, char *name1, char *value1, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id);
void add_cadvisor_metric_int(char *mname, int64_t val, char *cntid, char *name, char *image, char *cad_id, char *name1, char *value1, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id);
void add_cadvisor_metric_double(char *mname, double val, char *cntid, char *name, char *image, char *cad_id, char *name1, char *value1, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id);
void add_cadvisor_metric_labels_uint(char *mname, uint64_t val, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id, const cadvisor_label_pair *extra, size_t nextra);
void add_cadvisor_metric_labels_int(char *mname, int64_t val, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id, const cadvisor_label_pair *extra, size_t nextra);
void add_cadvisor_metric_labels_double(char *mname, double val, char *cntid, char *name, char *image, char *cad_id, char *kubenamespace, char *kubepod, char *kubecontainer, char *libvirt_id, const cadvisor_label_pair *extra, size_t nextra);

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
