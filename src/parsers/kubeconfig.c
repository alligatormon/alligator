#include <stdio.h>
#include <jansson.h>
#include "metric/namespace.h"
#include "common/yaml.h"
#include "events/context_arg.h"
#include "common/aggregator.h"
#include "common/http.h"
#include "common/base64.h"
#include "x509/type.h"
#include "events/fs_read.h"
#include "main.h"
void kubeconfig_handler(char *metrics, size_t size, context_arg *carg)
{
	json_t *root;
	json_error_t error;
	char* json = yaml_str_to_json_str(metrics);
	root = json_loads(json, 0, &error);
	free(json);
	if (!root) {
		fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
		return;
	}

	json_t *users = json_object_get(root, "users");
	size_t arr_sz = json_array_size(users);

	for (uint64_t i = 0; i < arr_sz; i++)
	{
		json_t *arr_obj = json_array_get(users, i);
		json_t *json_name = json_object_get(arr_obj, "name");
		char *name = (char*)json_string_value(json_name);
		if (!name)
			continue;

		if (carg->log_level > 0)
			printf("kubeconfig parse user name %s\n", name);
		json_t *users_user = json_object_get(arr_obj, "user");

		json_t *client_certificate_data = json_object_get(users_user, "client-certificate-data");
		char *cert = (char*)json_string_value(client_certificate_data);
		if (cert)
		{
			if (carg->log_level > 0)
				puts("> found client-certificate-data base64 data");
			//printf("cert is %s\n", cert);

			size_t outlen;
			char *cert_decoded = base64_decode(cert, json_string_length(client_certificate_data), &outlen);
			//printf("decoded cert is %s\n", cert_decoded);
			pem_check_cert(cert_decoded, outlen-1, NULL, (char*)carg->filename);

			free(cert_decoded);
		}

		json_t *client_certificate = json_object_get(users_user, "client-certificate");
		char *cert_path = (char*)json_string_value(client_certificate);
		if (cert_path)
		{
			if (carg->log_level > 0)
				printf("> found client-certificate path: %s\n", cert_path);
			char *filename = strdup(cert_path);
			read_from_file(filename, 0, pem_check_cert, NULL);
		}
	}

	json_decref(root);
	carg->parser_status = 1;
}

void kubeconfig_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("kubeconfig");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = kubeconfig_handler;
	actx->handler[0].validator = NULL;
	actx->handler[0].mesg_func = NULL;
	strlcpy(actx->handler[0].key, "kubeconfig", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
