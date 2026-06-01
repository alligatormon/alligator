#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <strings.h>
#include "kubernetes_common.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "api/api.h"
#include "main.h"
#include "events/context_arg.h"

static alligator_ht *kube_active_targets;
static alligator_ht *kube_seen_targets;
static uint8_t kube_reconcile_active;

void kubernetes_endpoint_port_free(void *funcarg, void *arg)
{
	kubernetes_endpoint_port *kubeport = arg;

	free(kubeport->name);
	free(kubeport->handler);
	free(kubeport->proto);
	free(kubeport->path);
	free(kubeport);
}

int kubernetes_endpoint_port_compare(const void *arg, const void *obj)
{
	char *s1 = (char *)arg;
	char *s2 = ((kubernetes_endpoint_port *)obj)->name;
	return strcmp(s1, s2);
}

static uint8_t kubernetes_truthy(const char *value)
{
	if (!value || !value[0])
		return 0;
	if (!strcmp(value, "0") || !strcasecmp(value, "false") || !strcasecmp(value, "off") || !strcasecmp(value, "no"))
		return 0;
	return 1;
}

static void kubernetes_operator_opts_add_namespace(kubernetes_operator_opts *opts, const char *namespace)
{
	if (!namespace || !namespace[0])
		return;

	for (uint64_t i = 0; i < opts->namespaces_size; ++i)
	{
		if (!strcmp(opts->namespaces[i], namespace))
			return;
	}

	opts->namespaces = realloc(opts->namespaces, sizeof(char *) * (opts->namespaces_size + 1));
	opts->namespaces[opts->namespaces_size++] = strdup(namespace);
}

static void kubernetes_operator_opts_parse_query(kubernetes_operator_opts *opts, const char *query)
{
	if (!query || !query[0])
		return;

	char *copy = strdup(query);
	char *saveptr = NULL;
	for (char *token = strtok_r(copy, "&", &saveptr); token; token = strtok_r(NULL, "&", &saveptr))
	{
		char *eq = strchr(token, '=');
		if (!eq)
			continue;
		*eq = '\0';
		char *key = token;
		char *value = eq + 1;

		if (!strcmp(key, "node_local"))
			opts->node_local = kubernetes_truthy(value);
		else if (!strcmp(key, "watch"))
		{
			opts->watch = kubernetes_truthy(value);
			opts->watch_configured = 1;
		}
		else if (!strcmp(key, "namespace") || !strcmp(key, "namespaces"))
			kubernetes_operator_opts_add_namespace(opts, value);
	}
	free(copy);
}

kubernetes_operator_opts *kubernetes_operator_opts_from_json(json_t *aggregate, host_aggregator_info *hi)
{
	kubernetes_operator_opts *opts = calloc(1, sizeof(*opts));

	if (aggregate)
	{
		json_t *node_local = json_object_get(aggregate, "node_local");
		if (node_local)
		{
			if (json_typeof(node_local) == JSON_STRING)
				opts->node_local = kubernetes_truthy(json_string_value(node_local));
			else
				opts->node_local = json_is_true(node_local);
		}

		json_t *namespaces = json_object_get(aggregate, "namespaces");
		if (namespaces && json_is_array(namespaces))
		{
			size_t arr_sz = json_array_size(namespaces);
			for (size_t i = 0; i < arr_sz; ++i)
			{
				json_t *entry = json_array_get(namespaces, i);
				if (json_is_string(entry))
					kubernetes_operator_opts_add_namespace(opts, json_string_value(entry));
			}
		}

		json_t *namespace = json_object_get(aggregate, "namespace");
		if (namespace && json_is_string(namespace))
			kubernetes_operator_opts_add_namespace(opts, json_string_value(namespace));

		json_t *watch = json_object_get(aggregate, "watch");
		if (watch)
		{
			if (json_typeof(watch) == JSON_STRING)
				opts->watch = kubernetes_truthy(json_string_value(watch));
			else
				opts->watch = json_is_true(watch);
			opts->watch_configured = 1;
		}
	}

	if (hi && hi->query)
		kubernetes_operator_opts_parse_query(opts, hi->query);

	if (!opts->watch_configured)
	{
		char *watch_env = getenv("ALLIGATOR_K8S_WATCH");
		if (watch_env && watch_env[0])
			opts->watch = kubernetes_truthy(watch_env);
		else
		{
			FILE *token_fd = fopen("/var/run/secrets/kubernetes.io/serviceaccount/token", "r");
			opts->watch = token_fd ? 1 : 0;
			if (token_fd)
				fclose(token_fd);
		}
	}

	opts->node_name = getenv("NODE_NAME");
	if (!opts->node_name || !opts->node_name[0])
		opts->node_name = getenv("ALLIGATOR_NODE_NAME");

	if (opts->node_local || (opts->node_name && opts->node_name[0]))
	{
		if (opts->node_name && opts->node_name[0])
			opts->node_local = 1;
	}

	if (opts->namespaces_size == 1)
	{
		opts->single_namespace_path = malloc(512);
		snprintf(opts->single_namespace_path, 511, "/api/v1/namespaces/%s/pods", opts->namespaces[0]);
	}

	return opts;
}

void kubernetes_operator_opts_free(kubernetes_operator_opts *opts)
{
	if (!opts)
		return;

	for (uint64_t i = 0; i < opts->namespaces_size; ++i)
		free(opts->namespaces[i]);
	free(opts->namespaces);
	free(opts->single_namespace_path);
	free(opts);
}

uint8_t kubernetes_pod_namespace_allowed(const char *namespace, kubernetes_operator_opts *opts)
{
	if (!opts || !opts->namespaces_size)
		return 1;
	if (!namespace)
		return 0;

	for (uint64_t i = 0; i < opts->namespaces_size; ++i)
	{
		if (!strcmp(opts->namespaces[i], namespace))
			return 1;
	}
	return 0;
}

uint8_t kubernetes_pod_node_allowed(json_t *spec, kubernetes_operator_opts *opts)
{
	if (!opts || !opts->node_local)
		return 1;
	if (!opts->node_name || !opts->node_name[0])
		return 1;

	json_t *node_name_json = json_object_get(spec, "nodeName");
	char *node_name = (char *)json_string_value(node_name_json);
	if (!node_name)
		return 0;
	return !strcmp(node_name, opts->node_name);
}

static uint8_t kubernetes_pod_label_enabled(json_t *labels)
{
	if (!labels)
		return 0;

	json_t *enabled = json_object_get(labels, KUBE_LABEL_ENABLED);
	if (!enabled)
		return 0;

	if (json_is_true(enabled))
		return 1;

	char *value = (char *)json_string_value(enabled);
	return value && kubernetes_truthy(value);
}

uint8_t kubernetes_pod_scrape_enabled(json_t *metadata, context_arg *carg,
	const char *pod_name, const char *namespace)
{
	if (metadata)
	{
		json_t *labels = json_object_get(metadata, "labels");
		if (kubernetes_pod_label_enabled(labels))
			return 1;
	}

	json_t *annotations = metadata ? json_object_get(metadata, "annotations") : NULL;
	if (!annotations)
		return 0;

	json_t *alligator = json_object_get(annotations, KUBE_SCRAPE_ANNOTATION);
	if (!alligator)
	{
		if (carg && carg->log_level > 1)
			printf("pod name %s, namespace %s: no %s\n", pod_name, namespace, KUBE_SCRAPE_ANNOTATION);
		return 0;
	}

	char *scrape = (char *)json_string_value(alligator);
	if (!scrape || !kubernetes_truthy(scrape))
	{
		if (carg && carg->log_level > 1)
			printf("pod name %s, namespace %s: %s disabled\n", pod_name, namespace, KUBE_SCRAPE_ANNOTATION);
		return 0;
	}
	return 1;
}

void kubernetes_ports_from_annotations(json_t *annotations, alligator_ht *hash, context_arg *carg)
{
	const char *annotation_key;
	json_t *annotation_value;

	json_object_foreach(annotations, annotation_key, annotation_value)
	{
		if (!strcmp(annotation_key, KUBE_SCRAPE_ANNOTATION))
			continue;
		if (strncmp(annotation_key, KUBE_ANNOTATION_PREFIX, strlen(KUBE_ANNOTATION_PREFIX)))
			continue;

		char *annotation = (char *)json_string_value(annotation_value);
		if (!annotation)
			continue;

		char metric_port_name[255];
		char type[255];
		char *ptr = (char *)annotation_key + strlen(KUBE_ANNOTATION_PREFIX);
		char *type_sep = strchr(ptr, '-');
		if (!type_sep)
			continue;

		uint64_t metric_port_size = type_sep - ptr;
		size_t metric_port_copy = metric_port_size < (sizeof(metric_port_name) - 1)
			? metric_port_size : (sizeof(metric_port_name) - 1);
		strlcpy(metric_port_name, ptr, metric_port_copy + 1);

		ptr = type_sep + 1;
		strlcpy(type, ptr, sizeof(type));

		if (carg && carg->log_level > 0)
			printf("\tkey: %s, metric_port_name: %s, type: %s, value: %s\n",
				annotation_key, metric_port_name, type, annotation);

		uint32_t metric_hash = tommy_strhash_u32(0, metric_port_name);
		kubernetes_endpoint_port *kubeport = alligator_ht_search(hash, kubernetes_endpoint_port_compare,
			metric_port_name, metric_hash);
		if (!kubeport)
		{
			kubeport = calloc(1, sizeof(*kubeport));
			kubeport->name = strdup(metric_port_name);
			alligator_ht_insert(hash, &(kubeport->node), kubeport, metric_hash);
		}

		if (!strcmp(type, "handler") && !kubeport->handler)
			kubeport->handler = strdup(annotation);
		if (!strcmp(type, "proto") && !kubeport->proto)
			kubeport->proto = strdup(annotation);
		if (!strcmp(type, "path") && !kubeport->path)
			kubeport->path = strdup(annotation);
	}
}

void kubernetes_target_key_make(char *key, size_t key_size, const char *namespace,
	const char *uid, const char *container, const char *port_name)
{
	snprintf(key, key_size, KUBE_TARGET_KEY_PREFIX "%s:%s:%s:%s",
		namespace ? namespace : "",
		uid ? uid : "",
		container ? container : "",
		port_name ? port_name : "");
}

void kubernetes_target_upsert(const char *target_key, const char *handler, const char *url,
	const char *namespace, const char *pod_name, const char *container_name,
	context_arg *carg)
{
	json_t *aggregate_root = json_object();
	json_t *aggregate_arr = json_array();
	json_t *aggregate_obj = json_object();

	json_array_object_insert(aggregate_root, "aggregate", aggregate_arr);
	json_array_object_insert(aggregate_arr, "", aggregate_obj);

	json_array_object_insert(aggregate_obj, "handler", json_string(handler));
	json_array_object_insert(aggregate_obj, "url", json_string(url));
	json_array_object_insert(aggregate_obj, "key", json_string(target_key));

	json_t *aggregate_add_label = json_object();
	json_array_object_insert(aggregate_obj, "add_label", aggregate_add_label);
	json_array_object_insert(aggregate_add_label, "instance", json_string(url));
	json_array_object_insert(aggregate_add_label, "kubernetes_namespace", json_string(namespace));
	json_array_object_insert(aggregate_add_label, "kubernetes_pod_name", json_string(pod_name));
	json_array_object_insert(aggregate_add_label, "kubernetes_container_name", json_string(container_name));
	if (carg && carg->instance)
		json_array_object_insert(aggregate_add_label, "alligator_instance", json_string(carg->instance));

	char *dvalue = json_dumps(aggregate_root, JSON_INDENT(2));
	if (carg && carg->log_level > 1)
		puts(dvalue);
	http_api_v1(NULL, NULL, dvalue);
	free(dvalue);
	json_decref(aggregate_root);
}

void kubernetes_target_delete(const char *target_key)
{
	smart_aggregator_del_key((char *)target_key);
}

typedef struct kube_target_node {
	char *key;
	char *url;
	tommy_node node;
} kube_target_node;

static int kube_target_node_compare(const void *arg, const void *obj)
{
	return strcmp((char *)arg, ((kube_target_node *)obj)->key);
}

static void kube_target_node_free(void *funcarg, void *arg)
{
	kube_target_node *node = arg;
	free(node->key);
	free(node->url);
	free(node);
}

static void kube_targets_init_once(void)
{
	if (kube_active_targets)
		return;
	kube_active_targets = calloc(1, sizeof(*kube_active_targets));
	alligator_ht_init(kube_active_targets);
}

void kubernetes_reconcile_begin(void)
{
	kube_targets_init_once();

	if (!kube_seen_targets)
	{
		kube_seen_targets = calloc(1, sizeof(*kube_seen_targets));
		alligator_ht_init(kube_seen_targets);
	}
	else
	{
		alligator_ht_foreach_arg(kube_seen_targets, kube_target_node_free, NULL);
		alligator_ht_done(kube_seen_targets);
		alligator_ht_init(kube_seen_targets);
	}

	kube_reconcile_active = 1;
}

static kube_target_node *kubernetes_active_target_get(const char *target_key)
{
	uint32_t hash = tommy_strhash_u32(0, target_key);
	return alligator_ht_search(kube_active_targets, kube_target_node_compare, (char *)target_key, hash);
}

void kubernetes_target_sync(const char *target_key, const char *url,
	const char *handler, const char *namespace, const char *pod_name,
	const char *container_name, context_arg *carg)
{
	if (!target_key || !url)
		return;

	kube_targets_init_once();
	uint32_t hash = tommy_strhash_u32(0, target_key);

	kube_target_node *active = kubernetes_active_target_get(target_key);
	if (!active)
	{
		kubernetes_target_upsert(target_key, handler, url, namespace, pod_name, container_name, carg);
		active = calloc(1, sizeof(*active));
		active->key = strdup(target_key);
		active->url = strdup(url);
		alligator_ht_insert(kube_active_targets, &(active->node), active, hash);
		return;
	}

	if (!active->url || strcmp(active->url, url))
	{
		kubernetes_target_delete(target_key);
		kubernetes_target_upsert(target_key, handler, url, namespace, pod_name, container_name, carg);
		free(active->url);
		active->url = strdup(url);
	}
}

void kubernetes_reconcile_mark(const char *target_key, const char *url,
	const char *handler, const char *namespace, const char *pod_name,
	const char *container_name, context_arg *carg)
{
	if (!kube_reconcile_active || !target_key || !url)
		return;

	kube_targets_init_once();
	uint32_t hash = tommy_strhash_u32(0, target_key);

	kube_target_node *seen = alligator_ht_search(kube_seen_targets, kube_target_node_compare,
		(char *)target_key, hash);
	if (!seen)
	{
		seen = calloc(1, sizeof(*seen));
		seen->key = strdup(target_key);
		seen->url = strdup(url);
		alligator_ht_insert(kube_seen_targets, &(seen->node), seen, hash);
	}
	else if (!seen->url || strcmp(seen->url, url))
	{
		free(seen->url);
		seen->url = strdup(url);
	}

	kube_target_node *active = kubernetes_active_target_get(target_key);
	if (!active)
	{
		kubernetes_target_upsert(target_key, handler, url, namespace, pod_name, container_name, carg);
		active = calloc(1, sizeof(*active));
		active->key = strdup(target_key);
		active->url = strdup(url);
		alligator_ht_insert(kube_active_targets, &(active->node), active, hash);
		return;
	}

	if (!active->url || strcmp(active->url, url))
	{
		kubernetes_target_delete(target_key);
		kubernetes_target_upsert(target_key, handler, url, namespace, pod_name, container_name, carg);
		free(active->url);
		active->url = strdup(url);
	}
}

static void kube_reconcile_remove_stale(void *funcarg, void *arg)
{
	kube_target_node *node = arg;
	uint32_t hash = tommy_strhash_u32(0, node->key);

	if (!alligator_ht_search(kube_seen_targets, kube_target_node_compare, node->key, hash))
	{
		kubernetes_target_delete(node->key);
		alligator_ht_remove_existing(kube_active_targets, &(node->node));
		kube_target_node_free(NULL, node);
	}
}

void kubernetes_reconcile_end(context_arg *carg)
{
	if (!kube_reconcile_active)
		return;

	alligator_ht_foreach_arg(kube_active_targets, kube_reconcile_remove_stale, NULL);
	kube_reconcile_active = 0;

	if (carg && carg->log_level > 0)
		printf("kubernetes reconcile complete, active targets: %zu\n", alligator_ht_count(kube_active_targets));
}

static void kubernetes_reconcile_pod_drop(json_t *item, context_arg *carg, kubernetes_operator_opts *opts)
{
	if (kube_reconcile_active || !item)
		return;
	kubernetes_reconcile_pod_deleted(item, carg, opts);
}

void kubernetes_reconcile_pod(json_t *item, context_arg *carg, kubernetes_operator_opts *opts)
{
	json_t *metadata = json_object_get(item, "metadata");
	json_t *spec = json_object_get(item, "spec");
	json_t *status = json_object_get(item, "status");

	json_t *namespace_json = json_object_get(metadata, "namespace");
	char *namespace = (char *)json_string_value(namespace_json);
	json_t *pod_name_json = json_object_get(metadata, "name");
	char *pod_name = (char *)json_string_value(pod_name_json);
	json_t *uid_json = json_object_get(metadata, "uid");
	char *uid = (char *)json_string_value(uid_json);

	if (!kubernetes_pod_namespace_allowed(namespace, opts))
	{
		kubernetes_reconcile_pod_drop(item, carg, opts);
		return;
	}
	if (!kubernetes_pod_node_allowed(spec, opts))
	{
		kubernetes_reconcile_pod_drop(item, carg, opts);
		return;
	}

	json_t *phase_json = json_object_get(status, "phase");
	char *phase = (char *)json_string_value(phase_json);
	if (phase && strcmp(phase, "Running"))
	{
		kubernetes_reconcile_pod_drop(item, carg, opts);
		return;
	}

	json_t *podIP_json = json_object_get(status, "podIP");
	char *podIP = (char *)json_string_value(podIP_json);
	if (!podIP || !podIP[0])
	{
		kubernetes_reconcile_pod_drop(item, carg, opts);
		return;
	}

	if (!kubernetes_pod_scrape_enabled(metadata, carg, pod_name, namespace))
	{
		kubernetes_reconcile_pod_drop(item, carg, opts);
		return;
	}

	json_t *annotations = json_object_get(metadata, "annotations");

	alligator_ht hash;
	alligator_ht_init(&hash);
	kubernetes_ports_from_annotations(annotations, &hash, carg);

	json_t *containers = json_object_get(spec, "containers");
	uint64_t containers_size = json_array_size(containers);
	for (uint64_t j = 0; j < containers_size; j++)
	{
		json_t *container = json_array_get(containers, j);
		json_t *name_json = json_object_get(container, "name");
		char *name = (char *)json_string_value(name_json);

		json_t *ports = json_object_get(container, "ports");
		size_t ports_size = json_array_size(ports);
		for (size_t k = 0; k < ports_size; k++)
		{
			json_t *port = json_array_get(ports, k);
			json_t *port_name_json = json_object_get(port, "name");
			char *port_name = (char *)json_string_value(port_name_json);
			if (!port_name)
				continue;

			uint32_t port_hash = tommy_strhash_u32(0, port_name);
			kubernetes_endpoint_port *kubeport = alligator_ht_search(&hash,
				kubernetes_endpoint_port_compare, port_name, port_hash);
			if (!kubeport || !kubeport->handler || !kubeport->proto)
				continue;

			json_t *port_containerPort_json = json_object_get(port, "containerPort");
			uint64_t containerPort = json_integer_value(port_containerPort_json);

			char url[255];
			const char *path = kubeport->path ? kubeport->path : "/";
			snprintf(url, sizeof(url) - 1, "%s://%s:%" PRIu64 "%s",
				kubeport->proto, podIP, containerPort, path);

			char target_key[512];
			kubernetes_target_key_make(target_key, sizeof(target_key), namespace, uid, name, port_name);
			if (kube_reconcile_active)
				kubernetes_reconcile_mark(target_key, url, kubeport->handler,
					namespace, pod_name, name, carg);
			else
				kubernetes_target_sync(target_key, url, kubeport->handler,
					namespace, pod_name, name, carg);
		}
	}

	alligator_ht_foreach_arg(&hash, kubernetes_endpoint_port_free, &hash);
	alligator_ht_done(&hash);
}

void kubernetes_pods_watch_path(char *path, size_t path_size, kubernetes_operator_opts *opts,
	const char *resource_version)
{
	const char *api_path = "/api/v1/pods/";

	if (opts && opts->single_namespace_path)
		api_path = opts->single_namespace_path;

	char query[768];
	snprintf(query, sizeof(query) - 1,
		"watch=1&timeoutSeconds=%d&labelSelector=%s",
		KUBE_WATCH_TIMEOUT_SEC, KUBE_LABEL_SELECTOR);

	if (opts && opts->node_local && opts->node_name && opts->node_name[0])
	{
		char node_q[256];
		snprintf(node_q, sizeof(node_q) - 1, "&fieldSelector=spec.nodeName%%3D%s", opts->node_name);
		strlcat(query, node_q, sizeof(query));
	}

	if (resource_version && resource_version[0])
	{
		char rv_q[128];
		snprintf(rv_q, sizeof(rv_q) - 1, "&resourceVersion=%s", resource_version);
		strlcat(query, rv_q, sizeof(query));
	}

	snprintf(path, path_size, "%s?%s", api_path, query);
}

static void kubernetes_targets_delete_by_uid(void *funcarg, void *arg)
{
	kube_target_node *node = arg;
	const char *prefix = funcarg;
	size_t prefix_len = strlen(prefix);

	if (!strncmp(node->key, prefix, prefix_len))
	{
		kubernetes_target_delete(node->key);
		alligator_ht_remove_existing(kube_active_targets, &(node->node));
		kube_target_node_free(NULL, node);
	}
}

void kubernetes_reconcile_pod_deleted(json_t *item, context_arg *carg, kubernetes_operator_opts *opts)
{
	json_t *metadata = json_object_get(item, "metadata");
	if (!metadata)
		return;

	json_t *namespace_json = json_object_get(metadata, "namespace");
	char *namespace = (char *)json_string_value(namespace_json);
	json_t *uid_json = json_object_get(metadata, "uid");
	char *uid = (char *)json_string_value(uid_json);

	if (!namespace || !uid)
		return;

	if (!kubernetes_pod_namespace_allowed(namespace, opts))
		return;

	kube_targets_init_once();

	char prefix[512];
	snprintf(prefix, sizeof(prefix), KUBE_TARGET_KEY_PREFIX "%s:%s:", namespace, uid);
	alligator_ht_foreach_arg(kube_active_targets, kubernetes_targets_delete_by_uid, prefix);

	if (carg && carg->log_level > 0)
		printf("kubernetes watch: removed targets for pod %s/%s\n", namespace, uid);
}

string *kubernetes_pods_list_mesg(host_aggregator_info *hi, kubernetes_operator_opts *opts,
	void *env, void *proxy_settings)
{
	char path[512];
	const char *api_path = "/api/v1/pods/";

	if (opts && opts->single_namespace_path)
		api_path = opts->single_namespace_path;

	if (opts && opts->node_local && opts->node_name && opts->node_name[0])
		snprintf(path, sizeof(path) - 1, "%s?labelSelector=%s&fieldSelector=spec.nodeName%%3D%s",
			api_path, KUBE_LABEL_SELECTOR, opts->node_name);
	else
		snprintf(path, sizeof(path) - 1, "%s?labelSelector=%s", api_path, KUBE_LABEL_SELECTOR);

	alligator_ht *merged_env = kubernetes_incluster_env(env);
	string *ret = string_init_add_auto(gen_http_query(0, hi->query, path, hi->host, "alligator",
		hi->auth, NULL, merged_env, proxy_settings, NULL));
	if (merged_env && merged_env != env)
	{
		alligator_ht_foreach_arg(merged_env, env_struct_free, merged_env);
		alligator_ht_done(merged_env);
		free(merged_env);
	}
	return ret;
}

static uint8_t kubernetes_env_has_auth(alligator_ht *env)
{
	if (!env)
		return 0;

	env_struct *es = alligator_ht_search(env, env_struct_compare, "Authorization",
		tommy_strhash_u32(0, "Authorization"));
	return es != NULL;
}

alligator_ht *kubernetes_incluster_env(alligator_ht *env)
{
	if (env && kubernetes_env_has_auth(env))
		return env;

	const char *token_path = "/var/run/secrets/kubernetes.io/serviceaccount/token";
	FILE *fd = fopen(token_path, "r");
	if (!fd)
		return env;

	char token[4096];
	size_t n = fread(token, 1, sizeof(token) - 1, fd);
	fclose(fd);
	if (!n)
		return env;
	token[n] = '\0';
	while (n > 0 && (token[n - 1] == '\n' || token[n - 1] == '\r'))
		token[--n] = '\0';

	alligator_ht *merged = env ? env_struct_duplicate(env) : calloc(1, sizeof(*merged));
	if (!env)
		alligator_ht_init(merged);

	char auth[4200];
	snprintf(auth, sizeof(auth) - 1, "Bearer %s", token);
	env_struct_push_alloc(merged, "Authorization", auth);
	return merged;
}
