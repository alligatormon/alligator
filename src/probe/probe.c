#include "stdlib.h"
#include "probe/probe.h"
#include "metric/labels.h"
#include "common/http.h"
#include "main.h"

int probe_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((probe_node*)obj)->name;
	return strcmp(s1, s2);
}

probe_node* probe_get(char *name)
{
	probe_node *pn = tommy_hashdyn_search(ac->probe, probe_compare, name, tommy_strhash_u32(0, name));
	if (pn)
		return pn;
	else
		return NULL;
}

void probe_push_json(json_t *probe)
{
	probe_node *pn = calloc(1, sizeof(*pn));

	json_t *jname = json_object_get(probe, "name");
	if (!jname)
		return;
	char *name = (char*)json_string_value(jname);
	pn->name = strdup(name);

	json_t *jprober = json_object_get(probe, "prober");
	if (!jprober)
		return;
	char *prober = (char*)json_string_value(jprober);

	pn->timeout = 5000;
	json_t *jtimeout = json_object_get(probe, "timeout");
	if (jtimeout)
		pn->timeout = json_integer_value(jtimeout);

	pn->method = HTTP_GET;
	json_t *jmethod = json_object_get(probe, "method");
	if (jmethod)
	{
		char *method = (char*)json_string_value(jmethod);
		if (!strcmp(method, "POST"))
			pn->method = HTTP_POST;
	}

	json_t *jfollow_redirects = json_object_get(probe, "follow_redirects");
	if (jfollow_redirects)
	{
		char *follow_redirects = (char*)json_string_value(jfollow_redirects);
		pn->follow_redirects = strtoll(follow_redirects, NULL, 10);
	}

	json_t *jloop = json_object_get(probe, "loop");
	if (jloop)
	{
		char *loop = (char*)json_string_value(jloop);
		pn->loop = strtoll(loop, NULL, 10);
	}

	json_t *jpercent = json_object_get(probe, "percent");
	if (jpercent)
	{
		char *percent = (char*)json_string_value(jpercent);
		pn->percent_success = strtod(percent, NULL);
	}

	json_t *jca_file = json_object_get(probe, "ca_file");
	if (jca_file)
	{
		char *ca_file = (char*)json_string_value(jca_file);
		if (ca_file)
			pn->ca_file = strdup(ca_file);
	}

	json_t *jcert_file = json_object_get(probe, "cert_file");
	if (jcert_file)
	{
		char *cert_file = (char*)json_string_value(jcert_file);
		if (cert_file)
			pn->cert_file = strdup(cert_file);
	}

	json_t *jkey_file = json_object_get(probe, "key_file");
	if (jkey_file)
	{
		char *key_file = (char*)json_string_value(jkey_file);
		if (key_file)
			pn->key_file = strdup(key_file);
	}

	json_t *jserver_name = json_object_get(probe, "server_name");
	if (jserver_name)
	{
		char *server_name = (char*)json_string_value(jserver_name);
		if (server_name)
			pn->server_name = strdup(server_name);
	}

	//uint64_t *valid_status_codes;
	//uint64_t valid_status_codes_size;
	json_t *jvalid_status_codes = json_object_get(probe, "valid_status_codes");
	if (jvalid_status_codes)
	{
		uint64_t valid_status_codes_size = pn->valid_status_codes_size = json_array_size(jvalid_status_codes);
		pn->valid_status_codes = calloc(1, sizeof(void*) * valid_status_codes_size);
		for (uint64_t i = 0, j = 0; i < valid_status_codes_size; i++)
		{
			json_t *valid_status_codes = json_array_get(jvalid_status_codes, i);
			char *vsc = (char*)json_string_value(valid_status_codes);
			if (vsc)
				pn->valid_status_codes[j++] = strdup(vsc);
		}
	}

	json_t *jtls = json_object_get(probe, "tls");
	char *tls = jtls ? (char*)json_string_value(jtls) : NULL;
	if (tls && strcmp(tls, "on"))
		tls = NULL;

	json_t *jtls_verify = json_object_get(probe, "tls_verify");
	char *tls_verify = jtls_verify ? (char*)json_string_value(jtls_verify) : NULL;
	if (tls_verify)
	{
		if (!strcmp(tls_verify, "on"))
			pn->tls_verify = 1;
	}

	if (!strcmp(prober, "icmp"))
	{
		pn->prober = APROTO_ICMP;
		pn->prober_str = "icmp";
		pn->scheme = "icmp://";
	}
	else if (!strcmp(prober, "http") && !tls)
	{
		pn->prober = APROTO_HTTP;
		pn->prober_str = "http";
		pn->scheme = "http://";
	}
	else if (!strcmp(prober, "http") && tls)
	{
		pn->prober = APROTO_HTTPS;
		pn->tls = 1;
		pn->prober_str = "http";
		pn->scheme = "https://";
	}
	else if (!strcmp(prober, "tcp") && !tls)
	{
		pn->prober = APROTO_TCP;
		pn->prober_str = "tcp";
		pn->scheme = "tcp://";
	}
	else if (!strcmp(prober, "tcp") && tls)
	{
		pn->prober = APROTO_TLS;
		pn->prober_str = "tcp";
		pn->tls = 1;
		pn->scheme = "tls://";
	}
	else if (!strcmp(prober, "udp") && !tls)
	{
		pn->prober = APROTO_UDP;
		pn->prober_str = "udp";
		pn->scheme = "udp://";
	}
	else 
	{
		free(pn->name);
		free(pn);
		return;
	}

	if (ac->log_level > 0)
		printf("create probe node name '%s' and prober '%s'\n", pn->name, prober);

	tommy_hashdyn_insert(ac->probe, &(pn->node), pn, tommy_strhash_u32(0, pn->name));
}

void probe_del_json(json_t *probe)
{
	json_t *jname = json_object_get(probe, "name");
	if (!jname)
		return;
	char *name = (char*)json_string_value(jname);

	probe_node *pn = tommy_hashdyn_search(ac->probe, probe_compare, name, tommy_strhash_u32(0, name));
	if (pn)
	{
		tommy_hashdyn_remove_existing(ac->probe, &(pn->node));

		if (pn->name)
			free(pn->name);
		if (pn->body)
			free(pn->body);
		if (pn->valid_status_codes)
		{
			uint64_t valid_status_codes_size = pn->valid_status_codes_size;
			for (uint64_t i = 0; i < valid_status_codes_size; i++)
			{
				free(pn->valid_status_codes[i]);
			}
			free(pn->valid_status_codes);
		}
		if (pn->source_ip_address)
			free(pn->source_ip_address);
		if (pn->ca_file)
			free(pn->ca_file);
		if (pn->cert_file)
			free(pn->cert_file);
		if (pn->key_file)
			free(pn->key_file);
		if (pn->server_name)
			free(pn->server_name);
		if (pn->http_proxy_url)
			free(pn->http_proxy_url);
		free(pn);
	}
}
