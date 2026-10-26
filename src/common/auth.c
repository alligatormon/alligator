#include <stdio.h>
#include <time.h>
#include <string.h>
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

static void auth_deny_copy(char *dst, size_t dstlen, const char *msg)
{
	if (!dst || !dstlen)
		return;
	snprintf(dst, dstlen, "%s", msg ? msg : "");
}

static int8_t http_check_authorize_group(context_arg *carg, http_reply_data *http_data, char *why, size_t why_len)
{
	const char *presented;
	char *values[AUTHORIZE_BIND_MAX];
	int filled;
	int8_t rc;
	uint8_t fail_index = 0;

	if (why && why_len)
		why[0] = 0;

	if (carg->authorize_validate == AUTHORIZE_VALIDATE_HEADER)
		presented = http_data->authorize_header;
	else
		presented = http_data->authorize_cookie;

	if (!carg->authorize_bind_count)
	{
		rc = authorize_validate(carg->authorize_secret, carg->authorize_period, presented, (int64_t)time(NULL));
		if (rc == 0)
			auth_deny_copy(why, why_len, "authorize code rejected");
		else if (rc < 0)
			auth_deny_copy(why, why_len, "authorize code missing");
		return rc;
	}

	if (!presented || !presented[0])
	{
		auth_deny_copy(why, why_len, "authorize code missing");
		return -1;
	}

	memset(values, 0, sizeof(values));
	filled = authorize_factors_fill(carg, http_data, values, &fail_index);
	if (filled != AUTHORIZE_FACTORS_OK)
	{
		char label[160];

		if (carg->authorize_binds && fail_index < carg->authorize_bind_count &&
		    authorize_bind_label(&carg->authorize_binds[fail_index], label, sizeof(label)) > 0 && why && why_len)
			snprintf(why, why_len, "authorize bind %s rejected", label);
		else
			auth_deny_copy(why, why_len, "authorize bind rejected");
		return 0;
	}
	rc = authorize_validate_bound(carg->authorize_secret, carg->authorize_period, presented, (int64_t)time(NULL),
		carg->authorize_binds, carg->authorize_bind_count, (const char *const *)values);
	authorize_factors_free(values, carg->authorize_bind_count);
	if (rc == 0)
		auth_deny_copy(why, why_len, "authorize code rejected");
	else if (rc < 0)
		auth_deny_copy(why, why_len, "authorize code missing");
	return rc;
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

	char why[160];
	int8_t rc;

	why[0] = 0;
	if (!carg || !http_data)
		return 1;
	if (carg->authorize_config_error)
	{
		auth_deny_copy(http_data->auth_deny, sizeof(http_data->auth_deny), "authorize config rejected");
		return 0;
	}

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
	{
		rc = http_check_static_auth(carg, http_data);
		if (rc == 0)
			auth_deny_copy(http_data->auth_deny, sizeof(http_data->auth_deny), "static auth rejected");
		else if (rc < 0)
			auth_deny_copy(http_data->auth_deny, sizeof(http_data->auth_deny), "static auth missing");
		return rc;
	}

	if (!static_on)
	{
		rc = http_check_authorize_group(carg, http_data, why, sizeof(why));
		if (rc < 1)
			auth_deny_copy(http_data->auth_deny, sizeof(http_data->auth_deny), why[0] ? why : "authorize rejected");
		return rc;
	}

	static_rc = http_check_static_auth(carg, http_data);
	authorize_rc = http_check_authorize_group(carg, http_data, why, sizeof(why));
	rc = http_combine_auth(static_rc, authorize_rc, carg->satisfy == SATISFY_ALL);
	if (rc < 1)
	{
		const char *msg = NULL;

		if (authorize_rc == 0)
			msg = why[0] ? why : "authorize code rejected";
		else if (static_rc == 0)
			msg = "static auth rejected";
		else if (authorize_rc < 0)
			msg = why[0] ? why : "authorize code missing";
		else if (static_rc < 0)
			msg = "static auth missing";
		if (msg)
			auth_deny_copy(http_data->auth_deny, sizeof(http_data->auth_deny), msg);
	}
	return rc;
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
