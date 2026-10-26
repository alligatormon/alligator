#include <time.h>
#include "events/context_arg.h"
#include "common/auth.h"
#include "common/authorize.h"
#include "common/logs.h"
#include "parsers/http_proto.h"

int auth_unit_compare(const void* arg, const void* obj)
{
		char *s1 = (char*)arg;
		char *s2 = ((auth_unit*)obj)->k;
		return strcmp(s1, s2);
}

static int authorize_is_configured(context_arg *carg)
{
	return carg && carg->authorize_secret && carg->authorize_secret[0] && carg->authorize_period;
}

static int8_t http_check_static_auth(context_arg *carg, http_reply_data *http_data)
{
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

static int8_t http_check_authorize_group(context_arg *carg, http_reply_data *http_data)
{
	const char *presented;

	if (carg->authorize_validate == AUTHORIZE_VALIDATE_HEADER)
		presented = http_data->authorize_header;
	else
		presented = http_data->authorize_cookie;

	return authorize_validate(carg->authorize_secret, carg->authorize_period, presented, (int64_t)time(NULL));
}

static int8_t http_combine_auth(int8_t static_rc, int8_t authorize_rc, int satisfy_all)
{
	if (satisfy_all)
	{
		if (static_rc == 1 && authorize_rc == 1)
			return 1;
		if (static_rc == 0 || authorize_rc == 0)
			return 0;
		return -1;
	}

	if (static_rc == 1 || authorize_rc == 1)
		return 1;
	if (static_rc == 0 || authorize_rc == 0)
		return 0;
	return -1;
}

int8_t http_check_auth(context_arg *carg, http_reply_data *http_data)
{
	int static_on;
	int authorize_on;
	int skip_authorize;
	int8_t static_rc;
	int8_t authorize_rc;
	char *header_name;
	char *cookie_name;

	if (!carg || !http_data)
		return 1;

	static_on = carg->auth_basic || carg->auth_bearer || carg->auth_other;
	authorize_on = authorize_is_configured(carg);
	skip_authorize = authorize_on && http_data->method == HTTP_METHOD_GET && http_uri_is_authorize(http_data->uri);

	carglog(carg, L_INFO, "http auth: basic:%d bearer:%d other:%d authorize:%d satisfy:%s\n",
		carg->auth_basic ? 1 : 0, carg->auth_bearer ? 1 : 0, carg->auth_other ? 1 : 0,
		authorize_on && !skip_authorize ? 1 : 0,
		carg->satisfy == SATISFY_ALL ? "all" : "any");

	if (authorize_on)
	{
		header_name = carg->authorize_header_name ? carg->authorize_header_name : "X-Alligator-Auth";
		cookie_name = carg->authorize_cookie_name ? carg->authorize_cookie_name : "alligator_auth";
		http_reply_authorize_extract(http_data, header_name, cookie_name);
	}

	if (!static_on && (!authorize_on || skip_authorize))
		return 1;

	if (static_on && (!authorize_on || skip_authorize))
		return http_check_static_auth(carg, http_data);

	if (!static_on)
		return http_check_authorize_group(carg, http_data);

	static_rc = http_check_static_auth(carg, http_data);
	authorize_rc = http_check_authorize_group(carg, http_data);
	return http_combine_auth(static_rc, authorize_rc, carg->satisfy == SATISFY_ALL);
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
