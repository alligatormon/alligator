#include "events/context_arg.h"
#include "common/auth.h"
#include "common/logs.h"

int auth_unit_compare(const void* arg, const void* obj)
{
		char *s1 = (char*)arg;
		char *s2 = ((auth_unit*)obj)->k;
		return strcmp(s1, s2);
}

int8_t http_check_auth(context_arg *carg, http_reply_data *http_data)
{
	carglog(carg, L_INFO, "http auth: basic:%d bearer:%d other:%d\n", carg->auth_basic ? 1:0, carg->auth_bearer ? 1:0, carg->auth_other ? 1:0);

	if (!carg)
		return 1;

	if (!carg->auth_basic && !carg->auth_bearer && !carg->auth_other)
		return 1;

	if (!http_data->auth_basic && !http_data->auth_bearer && !http_data->auth_other)
		return -1;


	if (http_data->auth_basic)
	{
		if (carg->auth_basic) {
			uint32_t hash = tommy_strhash_u32(0, http_data->auth_basic);
			auth_unit *au = alligator_ht_search(carg->auth_basic, auth_unit_compare, http_data->auth_basic, hash);
			if (au)
				return 1;
		}
		else
			return -1;
	}

	if (http_data->auth_bearer)
	{
		if (carg->auth_bearer) {
			uint32_t hash = tommy_strhash_u32(0, http_data->auth_bearer);
			auth_unit *au = alligator_ht_search(carg->auth_bearer, auth_unit_compare, http_data->auth_bearer, hash);
			if (au)
				return 1;
		}
		else
			return -1;
	}

	if (http_data->auth_other)
	{
		if (carg->auth_other) {
			uint32_t hash = tommy_strhash_u32(0, http_data->auth_other);
			auth_unit *au = alligator_ht_search(carg->auth_other, auth_unit_compare, http_data->auth_other, hash);
			if (au)
				return 1;
		}
		else
			return -1;
	}

	return 0;
}

int http_auth_push(alligator_ht *auth_context, char *key) {
	auth_unit *au = calloc(1, sizeof(*au));
	au->k = strdup(key);
	alligator_ht_insert(auth_context, &(au->node), au, tommy_strhash_u32(0, au->k));
	return 1;
}

void http_auth_copy_foreach(void *funcarg, void* arg)
{
	auth_unit *au = arg;
	alligator_ht *hash = funcarg;
	http_auth_push(hash, au->k);
}

alligator_ht *http_auth_copy(alligator_ht *src) {
	if (!src)
		return src;

	alligator_ht *dst = alligator_ht_init(NULL);
	alligator_ht_foreach_arg(src, http_auth_copy_foreach, dst);
	return dst;
}

int http_auth_delete(alligator_ht *auth_context, char *key) {
	uint32_t hash = tommy_strhash_u32(0, key);
	auth_unit *au = alligator_ht_search(auth_context, auth_unit_compare, key, hash);
	if (!au)
		return 0;

	alligator_ht_remove_existing(auth_context, &(au->node));
	free(au->k);
	free(au);
	return 1;
}

void http_auth_foreach_free(void *funcarg, void* arg)
{
	auth_unit *au = arg;
	free(au->k);
	free(au);
}
