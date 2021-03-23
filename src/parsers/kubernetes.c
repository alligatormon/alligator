#include <stdio.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "main.h"

typedef struct kubernetes_endpoint_port {
	char *name;
	char *handler;
	char *proto;
	char *path;

	tommy_node node;
} kubernetes_endpoint_port;

void kubernetes_endpoint_port_free(void *funcarg, void* arg)
{
	tommy_hashdyn *hash = funcarg;
	kubernetes_endpoint_port *kubeport = arg;

	tommy_hashdyn_remove_existing(hash, &(kubeport->node));
	free(kubeport->name);

	if (kubeport->handler)
		free(kubeport->handler);

	if (kubeport->proto)
		free(kubeport->proto);

	if (kubeport->path)
		free(kubeport->path);

	free(kubeport);
}

int kubernetes_endpoint_port_compare(const void* arg, const void* obj)
{
        char *s1 = (char*)arg;
        char *s2 = ((kubernetes_endpoint_port*)obj)->name;
        return strcmp(s1, s2);
}

void kubernetes_ingress_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	json_t *items = json_object_get(root, "items");
	uint64_t items_size = json_array_size(items);
	uint64_t i, j;
	for (i = 0; i < items_size; i++)
	{
		json_t *item = json_array_get(items, i);
		json_t *spec = json_object_get(item, "spec");
		json_t *rules = json_object_get(spec, "rules");
		uint64_t rules_size = json_array_size(rules);
		for (j = 0; j < rules_size; j++)
		{
			json_t *rule = json_array_get(rules, j);
			json_t *host_json = json_object_get(rule, "host");
			char *host = (char*)json_string_value(host_json);
			if (carg->log_level > 0)
				printf("ingress host is %s\n", host);

			json_t *http = json_object_get(rule, "http");
			char *keyhost = malloc((json_string_length(host_json)+10));
			snprintf(keyhost, (json_string_length(host_json)+9), "http://%s", host);

			json_t *aggregate_root = json_object();
			json_t *aggregate_arr = json_array();
			json_t *aggregate_obj = json_object();
			json_t *aggregate_handler = json_string(strdup("http"));
			json_t *aggregate_follow_redirects = json_integer(carg->follow_redirects-1);
			json_t *aggregate_url = json_string(keyhost);
			json_t *aggregate_name = json_string(strdup(keyhost));
			json_t *aggregate_add_label = json_object();
			json_array_object_insert(aggregate_root, "aggregate", aggregate_arr);
			json_array_object_insert(aggregate_arr, "", aggregate_obj);
			if (http)
				json_array_object_insert(aggregate_obj, "handler", aggregate_handler);
			else
			{
				json_decref(aggregate_root);
				continue;
			}

			json_array_object_insert(aggregate_obj, "url", aggregate_url);
			json_array_object_insert(aggregate_obj, "add_label", aggregate_add_label);
			json_array_object_insert(aggregate_obj, "follow_redirects", aggregate_follow_redirects);
			json_array_object_insert(aggregate_add_label, "name", aggregate_name);
			char *dvalue = json_dumps(aggregate_root, JSON_INDENT(2));
			if (carg->log_level > 1)
				puts(dvalue);
			http_api_v1(NULL, NULL, dvalue);
			free(dvalue);
			json_decref(aggregate_root);
		}
	}

	json_decref(root);
}

void kubernetes_endpoint_handler(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	tommy_hashdyn *hash = calloc(1, sizeof(*hash));
	tommy_hashdyn_init(hash);

	json_t *items = json_object_get(root, "items");
	uint64_t items_size = json_array_size(items);
	uint64_t i, j, k;
	for (i = 0; i < items_size; i++)
	{
		json_t *item = json_array_get(items, i);

		json_t *metadata = json_object_get(item, "metadata");
		json_t *namespace_json = json_object_get(metadata, "namespace");
		char *namespace = (char*)json_string_value(namespace_json);
		json_t *pod_name_json = json_object_get(metadata, "name");
		char *pod_name = (char*)json_string_value(pod_name_json);

		json_t *spec = json_object_get(item, "spec");

		json_t *containers = json_object_get(spec, "containers");
		json_t *status = json_object_get(item, "status");
		json_t *podIP_json = json_object_get(status, "podIP");
		char *podIP = (char*)json_string_value(podIP_json);

		json_t *annotations = json_object_get(metadata, "annotations");
		json_t *alligator = json_object_get(annotations, "alligator/scrape");
		if (!alligator)
		{
			if (carg->log_level > 1)
				printf("pod name %s, namespace %s: no annotations:alligator/scrape, show more\n", pod_name, namespace);
			continue;
		}
		else
		{
			char *scrape = (char*)json_string_value(alligator);
			if (!strcmp(scrape, "false"))
			{
				if (carg->log_level > 1)
					printf("pod name %s, namespace %s: annotations:alligator/scrape disabled, show more\n", pod_name, namespace);
				continue;
			}
		}

		const char *annotation_key;
		json_t *annotation_value;
		json_object_foreach(annotations, annotation_key, annotation_value)
		{
			if (!strcmp(annotation_key, "alligator/scrape"))
				continue;

			char *annotation = (char*)json_string_value(annotation_value);
			char metric_port_name[255];
			char type[255];

			char *ptr = (char*)annotation_key+10;
			uint64_t metric_port_size = strcspn(ptr, "-");
			// alligator/<name>-(handler|proto|path)
			strlcpy(metric_port_name, ptr, metric_port_size+1);

			ptr += metric_port_size+1;
			strlcpy(type, ptr, 254);

			if (carg->log_level > 0)
				printf("\tkey: %s, metric_port_name: %s, type: %s, value: %s \n", annotation_key, metric_port_name, type, annotation);

			uint32_t metric_hash = tommy_strhash_u32(0, metric_port_name);
			kubernetes_endpoint_port *kubeport = tommy_hashdyn_search(hash, kubernetes_endpoint_port_compare, metric_port_name, metric_hash);
			if (!kubeport)
			{
				kubeport = calloc(1, sizeof(*kubeport));
				kubeport->name = strdup(metric_port_name);
				tommy_hashdyn_insert(hash, &(kubeport->node), kubeport, metric_hash);
			}
			if (!strcmp(type, "handler") && !kubeport->handler)
				kubeport->handler = strdup(annotation);
			if (!strcmp(type, "proto") && !kubeport->proto)
				kubeport->proto = strdup(annotation);
			if (!strcmp(type, "path") && !kubeport->path)
				kubeport->path = strdup(annotation);
		}

		uint64_t containers_size = json_array_size(containers);
		for (j = 0; j < containers_size; j++)
		{
			json_t *container = json_array_get(containers, j);
			json_t *name_json = json_object_get(container, "name");
			char *name = (char*)json_string_value(name_json);
			if (carg->log_level > 0)
				printf("\tname is %s\n", name);

			json_t* ports = json_object_get(container, "ports");
			size_t ports_size = json_array_size(ports);
			for (k = 0; k < ports_size; k++)
			{
				json_t *port = json_array_get(ports, k);
				json_t *port_name_json = json_object_get(port, "name");
				char *port_name = (char*)json_string_value(port_name_json);
				
				uint32_t port_hash = tommy_strhash_u32(0, port_name);
				kubernetes_endpoint_port *kubeport = tommy_hashdyn_search(hash, kubernetes_endpoint_port_compare, port_name, port_hash);
				if (kubeport && kubeport->handler && kubeport->proto)
				{
					if (carg->log_level > 0)
						printf("kube port %p: %s, proto: %s, handler: %s, path: %s\n", kubeport, port_name, kubeport->proto, kubeport->handler, kubeport->path);
				}
				else
				{
					if (carg->log_level > 0)
						printf("kubeport %s not found\n", port_name);
					continue;
				}
				json_t *port_containerPort_json = json_object_get(port, "containerPort");
				uint64_t containerPort = json_integer_value(port_containerPort_json);
				if (carg->log_level > 1)
					printf("\t\tnamespace: %s, pod_name: %s, container_name: %s, container port: %"u64", IP: %s, handler: %s, proto: %s\n", namespace, pod_name, name, containerPort, podIP, kubeport->handler, kubeport->proto);

				char url[255];
				snprintf(url, 254, "%s://%s:%"u64"%s", kubeport->proto, podIP, containerPort, kubeport->path);

				json_t *aggregate_root = json_object();
				json_t *aggregate_arr = json_array();
				json_t *aggregate_obj = json_object();
				json_t *aggregate_handler = json_string(strdup(kubeport->handler));

				json_t *aggregate_url = json_string(strdup(url));
				json_t *aggregate_name = json_string(strdup(url));
				json_t *aggregate_add_label = json_object();
				json_array_object_insert(aggregate_root, "aggregate", aggregate_arr);
				json_array_object_insert(aggregate_arr, "", aggregate_obj);

				json_array_object_insert(aggregate_obj, "handler", aggregate_handler);
				json_array_object_insert(aggregate_obj, "url", aggregate_url);
				json_array_object_insert(aggregate_obj, "add_label", aggregate_add_label);
				json_array_object_insert(aggregate_add_label, "instance", aggregate_name);
				json_array_object_insert(aggregate_add_label, "kubernetes_namespace", json_string(strdup(namespace)));
				json_array_object_insert(aggregate_add_label, "kubernetes_pod_name", json_string(strdup(pod_name)));
				json_array_object_insert(aggregate_add_label, "kubernetes_container_name", json_string(strdup(name)));
				const char *dvalue = json_dumps(aggregate_root, JSON_INDENT(2));
				if (carg->log_level > 1)
					puts(dvalue);
				http_api_v1(NULL, NULL, dvalue);
				json_decref(aggregate_root);
			}
		}

		tommy_hashdyn_foreach_arg(hash, kubernetes_endpoint_port_free, hash);
	}

	json_decref(root);
	tommy_hashdyn_done(hash);
	free(hash);
}

string* kubernetes_ingress_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(gen_http_query(0, hi->query, "/apis/extensions/v1beta1/ingresses", hi->host, "alligator", hi->auth, 1, NULL, env, proxy_settings), 0, 0);
}

string* kubernetes_endpoint_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	return string_init_add(gen_http_query(0, hi->query, "/api/v1/pods/", hi->host, "alligator", hi->auth, 1, NULL, env, proxy_settings), 0, 0);
}

void kubernetes_ingress_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("kubernetes_ingress");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = kubernetes_ingress_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = kubernetes_ingress_mesg;
	strlcpy(actx->handler[0].key,"kubernetes_ingress", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

void kubernetes_endpoint_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("kubernetes_endpoint");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = kubernetes_endpoint_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = kubernetes_endpoint_mesg;
	strlcpy(actx->handler[0].key,"kubernetes_endpoint", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
