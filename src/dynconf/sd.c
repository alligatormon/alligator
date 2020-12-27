#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <uv.h>
#include <jansson.h>
#include "main.h"
#include "common/base64.h"
#include "common/selector.h"
#include "common/json_parser.h"
#include "common/http.h"

#ifdef __linux__
#include "zookeeper.h"
#define SD_ZOOKEEPER 1
#define SD_NONE 0
typedef struct sd_handler
{
	uint8_t sd_system;
	zhandle_t *zh;
	char *hostname;
	char *port;
	char *prefix;
	char *auth_cert;
	uint64_t auth_certlen;
	char *auth_schema;
	int64_t timeout;
} sd_handler;


/**
 *	* In this example this method gets the cert for your
 *	 *	 environment -- you must provide
 *		*/
//char *foo_get_cert_once(char* id) { return 0; }

/** Watcher function -- empty for this example, not something you should
 *	* do in real code */
//void watcher(zhandle_t *zzh, int type, int state, const char *path, void *watcherCtx) {}

//int main_old(int argc, char argv)
//{
//	zhandle_t *zh = NULL;
//	char buffer[512];
//	char p[2048];
//	char *cert=0;
//	char appId[64];
//
//	strcpy(appId, "example.foo_test");
//	cert = foo_get_cert_once(appId);
//	if(cert!=0) {
//		fprintf(stderr, "Certificate for appid [%s] is [%s]\n",appId,cert);
//		strncpy(p,cert, sizeof(p)-1);
//		free(cert);
//	} else {
//		fprintf(stderr, "Certificate for appid [%s] not found\n",appId);
//		strcpy(p, "dummy");
//	}
//
//
//	zh = zookeeper_init("localhost:2181", watcher, 10000, 0, 0, 0);
//	//zh = zookeeper_init("10.12.11.13:2181", watcher, 5000, 0, 0, 0);
//		printf("errno %d\n", errno);
//	while (!zh || !errno) {
//		zh = zookeeper_init("10.12.11.13:2181", watcher, 5000, 0, 0, 0);
//		printf("errno %d\n", errno);
//		sleep (2);
//		//return errno;
//	}
//	//if(zoo_add_auth(zh,"foo",p,strlen(p),0,0)!=ZOK)
//	//	return 2;
//
//	int8_t i = 10;
//	while (i--)
//	{
//		char writebuf[1200];
//		snprintf(writebuf, 1200, "alue: %d", i);
//		int rc = zoo_create(zh,"/xyz",writebuf, strlen(writebuf), &ZOO_OPEN_ACL_UNSAFE, 0, buffer, sizeof(buffer)-1);
//
//		/** this operation will fail with a ZNOAUTH error */
//		int buflen= sizeof(buffer);
//		struct Stat stat;
//		rc = zoo_get(zh, "/xyz", 0, buffer, &buflen, &stat);
//		if (rc) {
//			fprintf(stderr, "Error %d\n", rc);
//		}
//		printf("read buffer: %s\n", buffer);
//
//		if (zoo_exists(zh, "xyz", 1, &stat) == ZOK)
//		{
//			rc = zoo_delete(zh, "/xyz", -1);
//			if (rc) {
//				fprintf(stderr, "Error %d\n", rc);
//			}
//		}
//		else
//		{
//			printf("no nodes with name /xyz\n");
//		}
//		sleep(2);
//	}
//
//	zookeeper_close(zh);
//	return 0;
//}

void sd_close(sd_handler *sdh)
{
	if (sdh->sd_system == SD_ZOOKEEPER && sdh->zh)
	{
		zookeeper_close(sdh->zh);
		sdh->zh = NULL;
		sdh->sd_system = SD_NONE;
	}
}

uint8_t sd_checkconnect(sd_handler *sdh)
{
	if (sdh->sd_system == SD_ZOOKEEPER)
	{
		if (!sdh->zh)
		{
			char buf[256];
			snprintf(buf, 256, "%s:%s", sdh->hostname, sdh->port);
			sdh->zh = zookeeper_init(buf, NULL, sdh->timeout, 0, 0, 0);
			if (!sdh->zh || !errno)
			{
				return 0;
				sd_close(sdh);
			}

			if (sdh->auth_schema)
			{
				int rc = zoo_add_auth(sdh->zh,sdh->auth_schema,sdh->auth_cert,sdh->auth_certlen,0,0);
				if (rc)
				{
					printf("zoo_add_auth: %s\n", zerror(rc));
					sd_close(sdh);
					return 0;
				}
			}
		}
	}
	return 1;
}

void sd_cat_data(sd_handler *sdh, char *path)
{
	char buffer[1048577];
	*buffer = 0;
	int buflen = 1048577;
	struct Stat stat;
	//printf("get node from %s\n", path);
	int rc = zoo_get(sdh->zh, path, 0, buffer, &buflen, &stat);
	if (rc)
		if (ac->log_level > 1)
			printf("zoo_get: %s\n", zerror(rc));

	buffer[buflen] = 0;
	if (rc == ZOK)
	{
		if (ac->log_level > 1)
			printf("==> %s\n", buffer);
		http_api_v1(NULL, NULL, buffer);
	}
}

void sd_cat_children(sd_handler *sdh, char *prefix, size_t len)
{
	if (sdh->sd_system == SD_ZOOKEEPER)
	{
		struct Stat stat;
		struct String_vector strings = {0, 0};
		if (*prefix == 0)
			zoo_get_children2(sdh->zh, "/", 0, &strings, &stat);
		else
			zoo_get_children2(sdh->zh, prefix, 0, &strings, &stat);

		uint64_t i;

		for (i=0; i<strings.count; i++)
		{
			size_t mlen = len + strlen(strings.data[i])+1;
			char *buf = malloc(mlen+1);
			snprintf(buf, mlen, "%s/%s", prefix, strings.data[i]);
			if (ac->log_level > 1)
				printf("> %s\n", buf);

			sd_cat_data(sdh, buf);

			sd_cat_children(sdh, buf, mlen);
			free(buf);
		}
		deallocate_String_vector(&strings);
	}
}

void sd_read_params(sd_handler *sdh)
{
	sd_cat_children(sdh, "", 1);
}

void sd_getconf(sd_handler *sdh)
{
	if (!sd_checkconnect(sdh))
		return;

	sd_read_params(sdh);
}

sd_handler *sd_zookeeper_init(char *hostname, char *port, int64_t timeout, char *auth_schema, char *auth_cert)
{
	extern aconf *ac;

	sd_handler *sdh = calloc(1, sizeof(*sdh));
	sdh->sd_system = SD_ZOOKEEPER;
	sdh->hostname = strdup(hostname);
	sdh->port = strdup(port);
	sdh->timeout = timeout;
	sdh->auth_schema = auth_schema;
	sdh->auth_cert = auth_cert;
	sdh->auth_certlen = auth_cert ? strlen(auth_cert) : 0;

	zoo_set_debug_level(ac->log_level > 4 ? 4 : ac->log_level);

	return sdh;
}

void sd_scrape_cb(void *arg)
{
	context_arg *carg = arg;
	//sd_handler *sdh = sd_zookeeper_init("localhost", "2181", 5000, 0, 0);
	//sd_handler *sdh = sd_zookeeper_init("localhost", "2181", 5000, "digest", "kafka:12345");
	if (carg->log_level > 0)
		printf("scrape zookeeper conf from host %s, port %s\n", carg->host, carg->port);
	sd_handler *sdh = sd_zookeeper_init(carg->host, carg->port, carg->timeout, "digest", "kafka:12345");
	sd_getconf(sdh);
	sd_close(sdh);
}
//
//void zk_scrape()
//{
//	uv_thread_t th;
//	uv_thread_create(&th, sd_scrape_cb, NULL);
//}
void zk_timer_without_thread(uv_timer_t* handle) {
	(void)handle;
	tommy_hashdyn_foreach(ac->zk_aggregator, sd_scrape_cb);
}


void zk_without_thread()
{
	uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(ac->loop, timer1);
	uv_timer_start(timer1, zk_timer_without_thread, ac->aggregator_startup, ac->aggregator_repeat);
}

void zk_client_handler()
{
	uv_thread_t th;
	uv_thread_create(&th, zk_without_thread, NULL);
}

char* zk_client(context_arg* carg)
{
	if (!carg)
		return NULL;

	carg->key = malloc(255);
	snprintf(carg->key, 255, "%s", carg->host);

	tommy_hashdyn_insert(ac->zk_aggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	return "zk";
}

void zk_client_del(context_arg* carg)
{
	if (!carg)
		return;

	tommy_hashdyn_remove_existing(ac->zk_aggregator, &(carg->node));
	carg_free(carg);
}

void sd_zk_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));
	//zk_data *data = calloc(1, sizeof(*data));

	actx->key = strdup("zookeeper-configuration");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);
	//actx->data = data;

	actx->handler[0].name = NULL;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key,"zookeeper-configuration", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

#endif

void sd_etcd_node(json_t *rnode)
{
	uint64_t i;
	size_t size = json_array_size(rnode);
	for (i = 0; i < size; i++)
	{
		json_t *node = json_array_get(rnode, i);

		json_t *value = json_object_get(node, "value");
		if (value)
		{
			char *dvalue = (char*)json_string_value(value);
			http_api_v1(NULL, NULL, dvalue);
		}

		//json_t *key = json_object_get(node, "key");
		//char *key_str = (char*)json_string_value(key);
		//printf("key is %s\n", key_str);

		json_t *cnode = json_object_get(node, "nodes");
		if (cnode)
			sd_etcd_node(cnode);
	}
}

void sd_etcd_configuration(char *conf, size_t conf_len, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(conf, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	json_t *etcd_node = json_object_get(root, "node");
	json_t *nodes = json_object_get(etcd_node, "nodes");
	sd_etcd_node(nodes);

	json_decref(root);
}

void sd_consul_configuration(char *conf, size_t conf_len, context_arg *carg)
{
	puts(conf);
	json_error_t error;
	json_t *root = json_loads(conf, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	uint64_t i;
	size_t size = json_array_size(root);
	for (i = 0; i < size; i++)
	{
		json_t *node = json_array_get(root, i);

		json_t *value = json_object_get(node, "Value");
		if (value)
		{
			char *value_str = (char*)json_string_value(value);
			size_t outlen;
			char *dvalue = base64_decode(value_str, strlen(value_str), &outlen);
			//printf("consul %s\n", dvalue);
			http_api_v1(NULL, NULL, dvalue);
		}
	}

	json_decref(root);
}

void sd_consul_discovery(char *conf, size_t conf_len, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(conf, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	const char *name;
	json_t *value;
	json_object_foreach(root, name, value)
	{
		if (ac->log_level > 1)
			printf("service id: %s\n", name);
		json_t *meta = json_object_get(value, "Meta");
		if (meta)
		{
			json_t *alligator_handler = json_object_get(meta, "alligator_handler");
			json_t *alligator_url = json_object_get(meta, "alligator_url");
			json_t *alligator_host = json_object_get(meta, "alligator_host");
			json_t *alligator_port = json_object_get(meta, "alligator_port");
			json_t *alligator_proto = json_object_get(meta, "alligator_proto");
			json_t *alligator_path = json_object_get(meta, "alligator_path");

			char *handler = (char*)json_string_value(alligator_handler);
			if (!handler)
				handler = "blackbox";
			char *url = (char*)json_string_value(alligator_url);
			char *host = NULL;
			char *port;
			char *proto;
			char *path;
			char port_c[6];
			char url_c[1024];

			if (!url)
			{
				host = (char*)json_string_value(alligator_host);
				if (!host)
				{
					json_t *address = json_object_get(value, "Address");
					host = (char*)json_string_value(address);
					if (!host)
					{
						if (ac->log_level > 1)
							printf("alligator_host for svc: %s not specified, skip\n", name);
						continue;
					}
				}

				port = (char*)json_string_value(alligator_port);
				if (!port)
				{
					json_t *aport = json_object_get(value, "Port");
					int64_t port_i = json_integer_value(aport);
					if (!port_i)
					{
						if (ac->log_level > 1)
							printf("alligator_port for svc: %s not specified, skip\n", name);
						continue;
					}

					snprintf(port_c, 5, "%"d64, port_i);
					port = port_c;
				}

				proto = (char*)json_string_value(alligator_proto);
				if (!proto)
				{
					if (ac->log_level > 1)
						printf("alligator_proto for svc: %s not specified, skip\n", name);
					continue;
				}

				path = (char*)json_string_value(alligator_path);

				snprintf(url_c, 1023, "%s://%s:%s%s", proto, host, port, path ? path : "");
				url = url_c;
			}

			//printf("handler: %s\n", handler);
			//printf("url: %s\n", url);
			json_t *aggregate_root = json_object();
			json_t *aggregate_arr = json_array();
			json_t *aggregate_obj = json_object();
			json_t *aggregate_handler = json_string(strdup(handler));
			json_t *aggregate_url = json_string(strdup(url));
			json_t *aggregate_name = json_string(strdup(name));
			json_t *aggregate_add_label = json_object();
			json_array_object_insert(aggregate_root, "aggregate", aggregate_arr);
			json_array_object_insert(aggregate_arr, "", aggregate_obj);
			json_array_object_insert(aggregate_obj, "handler", aggregate_handler);
			json_array_object_insert(aggregate_obj, "url", aggregate_url);
			json_array_object_insert(aggregate_obj, "add_label", aggregate_add_label);
			json_array_object_insert(aggregate_add_label, "name", aggregate_name);
			const char *dvalue = json_dumps(aggregate_root, JSON_INDENT(2));
			http_api_v1(NULL, NULL, dvalue);
		}
	}

	json_decref(root);
}

string* sd_etcd_mesg(host_aggregator_info *hi, void *arg)
{
	char *replacedquery = malloc(255);
	char *path = "";
	snprintf(replacedquery, 255, "%sv2/keys%s?recursive=true", hi->query, path ? path : "");
	return string_init_add(gen_http_query(0, replacedquery, NULL, hi->host, "alligator", hi->auth, 1, NULL), 0, 0);
}

void sd_etcd_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("etcd-configuration");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = sd_etcd_configuration;
	actx->handler[0].validator = json_validator;
	actx->handler[0].mesg_func = sd_etcd_mesg;
	strlcpy(actx->handler[0].key,"etcd-configuration", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

string* sd_consul_configuration_mesg(host_aggregator_info *hi, void *arg)
{
	return string_init_add(gen_http_query(0, hi->query, "/v1/kv/?recurse", hi->host, "alligator", hi->auth, 1, "1.0"), 0, 0);
}

void sd_consul_configuration_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("consul-configuration");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = sd_consul_configuration;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = sd_consul_configuration_mesg;
	strlcpy(actx->handler[0].key, "consul-configuration", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}

string* sd_consul_discovery_mesg(host_aggregator_info *hi, void *arg)
{
	return string_init_add(gen_http_query(0, hi->query, "/v1/agent/services", hi->host, "alligator", hi->auth, 1, "1.0"), 0, 0);
}

void sd_consul_discovery_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("consul-discovery");
	actx->handlers = 1;
	actx->handler = malloc(sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = sd_consul_discovery;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = sd_consul_discovery_mesg;
	strlcpy(actx->handler[0].key, "consul-discovery", 255);

	tommy_hashdyn_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
