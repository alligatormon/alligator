#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <uv.h>
#include <jansson.h>
#include "main.h"
#include "common/base64.h"
//#include "common/url.h"
//#include "common/http.h"
//#include "common/json_parser.h"

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
		printf("zoo_get: %s\n", zerror(rc));

	buffer[buflen] = 0;
	if (rc == ZOK)
		printf("==> %s\n", buffer);
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

void sd_scrape_cb()
{
	//sd_handler *sdh = sd_zookeeper_init("localhost", "2181", 5000, 0, 0);
	sd_handler *sdh = sd_zookeeper_init("localhost", "2181", 5000, "digest", "kafka:12345");
	//sd_handler *sdh = sd_zookeeper_init("10.10.10.10", "2181", 1000);
	sd_getconf(sdh);
	sd_close(sdh);
}

void sd_scrape()
{
	uv_thread_t th;
	uv_thread_create(&th, sd_scrape_cb, NULL);
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
			char *value_str = (char*)json_string_value(value);
			printf("%s\n", value_str);
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
	puts("sd_consul_configuration");
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
			printf("consul %s\n", dvalue);
		}
	}

	json_decref(root);
}

void sd_consul_discovery(char *conf, size_t conf_len, context_arg *carg)
{
	puts("sd_consul_discovery");
	json_error_t error;
	json_t *root = json_loads(conf, 0, &error);
	if (!root)
	{
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	const char *key;
	json_t *value;
	json_object_foreach(root, key, value)
	{
		printf("service id: %s\n", key);
		json_t *meta = json_object_get(value, "Meta");
		if (meta)
		{
			json_t *alligator_handler = json_object_get(meta, "alligator_handler");
			json_t *alligator_url = json_object_get(meta, "alligator_url");
			if (alligator_handler && alligator_url)
			{
				char *handler = (char*)json_string_value(alligator_handler);
				char *url = (char*)json_string_value(alligator_url);
				printf("handler: %s\n", handler);
				printf("url: %s\n", url);

				//host_aggregator_info *hi = parse_url(url, strlen(url));
				//char *query = gen_http_query(0, hi->query, hi->host, "alligator", hi->auth, 1);
				//context_arg *carg = context_arg_fill(NULL, 0, hi, json_handler, query, NULL, json_check);
				//smart_aggregator(carg);
			}
		}
	}

	json_decref(root);
}
