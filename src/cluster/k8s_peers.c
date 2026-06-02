#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <jansson.h>
#include "cluster/type.h"
#include "cluster/k8s_peers.h"
#include "parsers/kubernetes_common.h"
#include "common/http.h"
#include "common/aggregator.h"
#include "events/context_arg.h"
#include "main.h"

static char *cluster_k8s_peers_service;
static char *cluster_k8s_peers_namespace;
static uint16_t cluster_k8s_peers_port = 1111;
static char *cluster_k8s_peers_cluster_name;

void cluster_k8s_peers_configure(void)
{
	char *service = getenv("ALLIGATOR_K8S_PEERS_SERVICE");
	if (!service || !service[0])
		return;

	cluster_k8s_peers_service = strdup(service);

	char *ns = getenv("ALLIGATOR_K8S_PEERS_NAMESPACE");
	if (!ns || !ns[0])
		ns = getenv("POD_NAMESPACE");
	cluster_k8s_peers_namespace = strdup((ns && ns[0]) ? ns : "default");

	char *port = getenv("ALLIGATOR_K8S_PEERS_PORT");
	if (port && port[0])
		cluster_k8s_peers_port = (uint16_t)strtoul(port, NULL, 10);

	char *cluster_name = getenv("ALLIGATOR_CLUSTER_NAME");
	if (cluster_name && cluster_name[0])
		cluster_k8s_peers_cluster_name = strdup(cluster_name);
}

static void cluster_k8s_peers_update_servers(cluster_node *cn, char **servers, uint64_t servers_size)
{
	if (!cn || !servers || !servers_size)
		return;

	for (uint64_t i = 0; i < cn->servers_size; ++i)
	{
		free(cn->servers[i].name);
		if (cn->servers[i].oprec)
			oplog_record_free(cn->servers[i].oprec);
	}
	free(cn->servers);

	cn->servers_size = servers_size;
	cn->servers = calloc(servers_size, sizeof(*cn->servers));
	for (uint64_t i = 0; i < servers_size; ++i)
	{
		cn->servers[i].name = strdup(servers[i]);
		cn->servers[i].oprec = oplog_record_init(cn->size ? cn->size : 1000);
		cn->servers[i].index = i;
		if (cn->servers[i].name && getenv("ALLIGATOR_INSTANCE") &&
			!strcmp(cn->servers[i].name, getenv("ALLIGATOR_INSTANCE")))
			cn->servers[i].is_me = 1;
	}
}

void cluster_k8s_peers_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
		return;

	cluster_node *cn = carg->data;
	if (!cn)
	{
		json_decref(root);
		return;
	}

	json_t *subsets = json_object_get(root, "subsets");
	if (!subsets)
	{
		json_decref(root);
		return;
	}

	char **servers = NULL;
	uint64_t servers_size = 0;

	size_t subsets_size = json_array_size(subsets);
	for (size_t i = 0; i < subsets_size; ++i)
	{
		json_t *subset = json_array_get(subsets, i);
		json_t *addresses = json_object_get(subset, "addresses");
		size_t addr_size = json_array_size(addresses);
		for (size_t j = 0; j < addr_size; ++j)
		{
			json_t *address = json_array_get(addresses, j);
			json_t *hostname = json_object_get(address, "hostname");
			char *host = (char *)json_string_value(hostname);
			if (!host || !host[0])
				continue;

			char server[512];
			snprintf(server, sizeof(server) - 1, "%s:%" PRIu16, host, cluster_k8s_peers_port);
			servers = realloc(servers, sizeof(char *) * (servers_size + 1));
			servers[servers_size++] = strdup(server);
		}
	}

	if (servers_size)
		cluster_k8s_peers_update_servers(cn, servers, servers_size);

	for (uint64_t i = 0; i < servers_size; ++i)
		free(servers[i]);
	free(servers);

	json_decref(root);
	carg->parser_status = 1;
}

static cluster_node *cluster_k8s_peers_pick_node;

static void cluster_k8s_peers_pick(void *funcarg, void *arg)
{
	if (cluster_k8s_peers_pick_node)
		return;
	cluster_node *candidate = arg;
	if (candidate && candidate->type == CLUSER_TYPE_SHAREDLOCK)
		cluster_k8s_peers_pick_node = candidate;
}

void cluster_k8s_peers_refresh(void)
{
	if (!cluster_k8s_peers_service || !ac->cluster)
		return;

	cluster_node *cn = NULL;
	if (cluster_k8s_peers_cluster_name)
		cn = cluster_get(cluster_k8s_peers_cluster_name);

	if (!cn && alligator_ht_count(ac->cluster))
	{
		cluster_k8s_peers_pick_node = NULL;
		alligator_ht_foreach_arg(ac->cluster, cluster_k8s_peers_pick, NULL);
		cn = cluster_k8s_peers_pick_node;
	}

	if (!cn)
		return;

	char path[512];
	snprintf(path, sizeof(path) - 1, "/api/v1/namespaces/%s/endpoints/%s",
		cluster_k8s_peers_namespace, cluster_k8s_peers_service);

	char url[512];
	snprintf(url, sizeof(url) - 1, "https://kubernetes.default.svc%s", path);

	char *query = gen_http_query(0, NULL, path, "kubernetes.default.svc", "alligator", NULL, NULL, NULL, NULL, NULL);
	alligator_ht *env = kubernetes_incluster_env(NULL);
	if (env)
	{
		free(query);
		query = gen_http_query(0, NULL, path, "kubernetes.default.svc", "alligator", NULL, NULL, env, NULL, NULL);
	}
	aggregator_oneshot(NULL, url, strlen(url), query, strlen(query),
		cluster_k8s_peers_handler, "cluster_k8s_peers", NULL, NULL, 0, cn,
		NULL, 0, NULL, env);
	free(query);
	if (env)
	{
		alligator_ht_foreach_arg(env, env_struct_free, env);
		alligator_ht_done(env);
		free(env);
	}
}
