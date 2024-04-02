#include "events/context_arg.h"
#include "common/aggregator.h"
#include "main.h"
#include "hdef.h"
#include "hsocket.h"
#include "herr.h"
#include "resolver/resolver.h"
#include "common/json_parser.h"

void resolver_recurse(void *funcarg, void* arg)
{
	dns_resource_records *dns_rr = arg;
	json_t *root = funcarg;
	json_t *dst = json_object();

	json_array_object_insert(root, dns_rr->key, dst);

	char *type_str = get_str_by_rrtype(dns_rr->type);
	if (!type_str)
		type_str = "";

	json_t *type_json = json_string(type_str);
	json_array_object_insert(dst, "type", type_json);

	json_t *rr = json_array();
	json_array_object_insert(dst, "rr", rr);
		
	for (uint64_t i = 0; i < dns_rr->size; ++i)
	{
		json_t *json_data = json_string((const char*)dns_rr->rr[i].data->s);
		json_array_object_insert(rr, NULL, json_data);
	}
}

json_t *resolver_get_data()
{
	json_t *ret = json_object();
	alligator_ht_foreach_arg(ac->resolver, resolver_recurse, ret);
	return ret;
}

string *resolver_get_api_response()
{
	string *ret = NULL;
	json_t *dst = resolver_get_data();

	char *dvalue = json_dumps(dst, JSON_INDENT(1));
	if (dvalue)
	{
		size_t dvalsize = strlen(dvalue);
		ret = string_init_add(dvalue, dvalsize, dvalsize);
	}

	json_decref(dst);

	return ret;
}
