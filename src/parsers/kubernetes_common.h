#pragma once
#include <jansson.h>
#include <stdint.h>
#include "events/context_arg.h"
#include "dstructures/ht.h"
#include "common/url.h"

#define KUBE_ANNOTATION_PREFIX "alligator/"
#define KUBE_SCRAPE_ANNOTATION "alligator/scrape"
#define KUBE_LABEL_ENABLED "alligator.io/enabled"
#define KUBE_LABEL_SELECTOR "alligator.io%2Fenabled%3Dtrue"
#define KUBE_WATCH_TIMEOUT_SEC 3600
#define KUBE_TARGET_KEY_PREFIX "k8s:"

typedef struct kubernetes_endpoint_port {
	char *name;
	char *handler;
	char *proto;
	char *path;
	tommy_node node;
} kubernetes_endpoint_port;

typedef struct kubernetes_operator_opts {
	uint8_t node_local;
	uint8_t watch;
	uint8_t watch_configured;
	char *node_name;
	char **namespaces;
	uint64_t namespaces_size;
	char *single_namespace_path;
} kubernetes_operator_opts;

void kubernetes_endpoint_port_free(void *funcarg, void *arg);
int kubernetes_endpoint_port_compare(const void *arg, const void *obj);

kubernetes_operator_opts *kubernetes_operator_opts_from_json(json_t *aggregate, host_aggregator_info *hi);
void kubernetes_operator_opts_free(kubernetes_operator_opts *opts);

uint8_t kubernetes_pod_namespace_allowed(const char *namespace, kubernetes_operator_opts *opts);
uint8_t kubernetes_pod_node_allowed(json_t *spec, kubernetes_operator_opts *opts);
uint8_t kubernetes_pod_scrape_enabled(json_t *metadata, context_arg *carg,
	const char *pod_name, const char *namespace);

void kubernetes_ports_from_annotations(json_t *annotations, alligator_ht *hash, context_arg *carg);

void kubernetes_target_key_make(char *key, size_t key_size, const char *namespace,
	const char *uid, const char *container, const char *port_name);

void kubernetes_target_upsert(const char *target_key, const char *handler, const char *url,
	const char *namespace, const char *pod_name, const char *container_name,
	context_arg *carg);

void kubernetes_target_sync(const char *target_key, const char *url,
	const char *handler, const char *namespace, const char *pod_name,
	const char *container_name, context_arg *carg);

void kubernetes_target_delete(const char *target_key);

void kubernetes_reconcile_begin(void);
void kubernetes_reconcile_mark(const char *target_key, const char *url,
	const char *handler, const char *namespace, const char *pod_name,
	const char *container_name, context_arg *carg);
void kubernetes_reconcile_end(context_arg *carg);

void kubernetes_reconcile_pod(json_t *item, context_arg *carg, kubernetes_operator_opts *opts);
void kubernetes_reconcile_pod_deleted(json_t *item, context_arg *carg, kubernetes_operator_opts *opts);

void kubernetes_pods_watch_path(char *path, size_t path_size, kubernetes_operator_opts *opts,
	const char *resource_version);

string *kubernetes_pods_list_mesg(host_aggregator_info *hi, kubernetes_operator_opts *opts,
	void *env, void *proxy_settings);

alligator_ht *kubernetes_incluster_env(alligator_ht *env);

void kubernetes_watch_start(host_aggregator_info *hi, kubernetes_operator_opts *opts);
uint8_t kubernetes_watch_active(void);

