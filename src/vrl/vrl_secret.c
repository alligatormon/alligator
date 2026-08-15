/*
 * Alligator host builtins: event secrets + semantic meaning (Vector-compatible).
 *
 *   get_secret(key) -> bytes | null
 *   set_secret(key, secret) -> null
 *   remove_secret(key) -> null
 *   set_semantic_meaning(target, meaning) -> null
 *     target is a string path (optional leading '.'); Vector path-expr form
 *     is accepted when passed as a string.
 *
 * State lives on vrl_stream (a->ctx->host) and is cleared between records.
 */

#define _GNU_SOURCE
#include "vrl/type.h"
#include "dstructures/ht.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
	alligator_ht_node node;
	char *key;
	char *value;
	size_t value_len;
} vrl_kv_entry;

static int vrl_kv_compare(const void *arg, const void *obj)
{
	return strcmp((const char *)arg, ((const vrl_kv_entry *)obj)->key);
}

static void vrl_kv_entry_free(void *arg)
{
	vrl_kv_entry *e = arg;
	if (!e)
		return;
	free(e->key);
	free(e->value);
	free(e);
}

static void vrl_kv_free_foreach(void *funcarg, void *arg)
{
	(void)funcarg;
	vrl_kv_entry_free(arg);
}

static void vrl_kv_map_clear(alligator_ht **map)
{
	if (!map || !*map)
		return;
	alligator_ht_foreach_arg(*map, vrl_kv_free_foreach, NULL);
	alligator_ht_done(*map);
	free(*map);
	*map = NULL;
}

static alligator_ht *vrl_kv_map_ensure(alligator_ht **map)
{
	if (!*map)
		*map = alligator_ht_init(NULL);
	return *map;
}

void vrl_stream_clear_secrets(vrl_stream *st)
{
	if (!st)
		return;
	vrl_kv_map_clear(&st->secrets);
	vrl_kv_map_clear(&st->semantic_meanings);
}

void vrl_stream_free_secrets(vrl_stream *st)
{
	vrl_stream_clear_secrets(st);
}

static vrl_stream *vrl_secret_stream(vrl_call_args *a, char **err)
{
	vrl_stream *st = (a && a->ctx) ? (vrl_stream *)a->ctx->host : NULL;
	if (!st) {
		*err = vrl_errf("secret builtins require an alligator vrl stream");
		return NULL;
	}
	return st;
}

static char *vrl_arg_bytes_dup(vrl_value *v)
{
	if (!v || v->type != VRL_BYTES || !v->u.bytes.data)
		return NULL;
	return strndup(v->u.bytes.data, v->u.bytes.len);
}

static vrl_status fn_get_secret(vrl_call_args *a, vrl_value **out, char **err)
{
	vrl_stream *st = vrl_secret_stream(a, err);
	if (!st)
		return VRL_ERR;
	vrl_value *key_v = vrl_arg(a, "key", 0);
	char *key = vrl_arg_bytes_dup(key_v);
	if (!key) {
		*err = vrl_errf("get_secret: key must be a string");
		return VRL_ERR;
	}
	vrl_kv_entry *e = NULL;
	if (st->secrets)
		e = alligator_ht_search(st->secrets, vrl_kv_compare, key,
					tommy_strhash_u32(0, key));
	free(key);
	if (!e) {
		*out = vrl_null();
		return VRL_OK;
	}
	*out = vrl_bytes(e->value, e->value_len);
	return VRL_OK;
}

static vrl_status fn_set_secret(vrl_call_args *a, vrl_value **out, char **err)
{
	vrl_stream *st = vrl_secret_stream(a, err);
	if (!st)
		return VRL_ERR;
	vrl_value *key_v = vrl_arg(a, "key", 0);
	vrl_value *sec_v = vrl_arg(a, "secret", 1);
	char *key = vrl_arg_bytes_dup(key_v);
	char *secret = vrl_arg_bytes_dup(sec_v);
	if (!key || !secret) {
		free(key);
		free(secret);
		*err = vrl_errf("set_secret: key and secret must be strings");
		return VRL_ERR;
	}
	alligator_ht *map = vrl_kv_map_ensure(&st->secrets);
	vrl_kv_entry *e = alligator_ht_search(map, vrl_kv_compare, key,
					      tommy_strhash_u32(0, key));
	if (e) {
		free(e->value);
		e->value = secret;
		e->value_len = strlen(secret);
		free(key);
	} else {
		e = calloc(1, sizeof(*e));
		e->key = key;
		e->value = secret;
		e->value_len = strlen(secret);
		alligator_ht_insert(map, &e->node, e, tommy_strhash_u32(0, e->key));
	}
	*out = vrl_null();
	return VRL_OK;
}

static vrl_status fn_remove_secret(vrl_call_args *a, vrl_value **out, char **err)
{
	vrl_stream *st = vrl_secret_stream(a, err);
	if (!st)
		return VRL_ERR;
	vrl_value *key_v = vrl_arg(a, "key", 0);
	char *key = vrl_arg_bytes_dup(key_v);
	if (!key) {
		*err = vrl_errf("remove_secret: key must be a string");
		return VRL_ERR;
	}
	if (st->secrets) {
		vrl_kv_entry *e = alligator_ht_search(st->secrets, vrl_kv_compare, key,
						     tommy_strhash_u32(0, key));
		if (e) {
			alligator_ht_remove_existing(st->secrets, &e->node);
			vrl_kv_entry_free(e);
		}
	}
	free(key);
	*out = vrl_null();
	return VRL_OK;
}

static vrl_status fn_set_semantic_meaning(vrl_call_args *a, vrl_value **out, char **err)
{
	vrl_stream *st = vrl_secret_stream(a, err);
	if (!st)
		return VRL_ERR;
	/* Accept string path (optional leading '.'); Vector path-expr needs a string here. */
	vrl_value *target_v = vrl_arg(a, "target", 0);
	vrl_value *meaning_v = vrl_arg(a, "meaning", 1);
	char *target = vrl_arg_bytes_dup(target_v);
	char *meaning = vrl_arg_bytes_dup(meaning_v);
	if (!target || !meaning) {
		free(target);
		free(meaning);
		*err = vrl_errf("set_semantic_meaning: target and meaning must be strings");
		return VRL_ERR;
	}
	const char *path = target;
	if (path[0] == '.')
		path++;

	alligator_ht *map = vrl_kv_map_ensure(&st->semantic_meanings);
	vrl_kv_entry *e = alligator_ht_search(map, vrl_kv_compare, path,
					      tommy_strhash_u32(0, (char *)path));
	if (e) {
		free(e->value);
		e->value = meaning;
		e->value_len = strlen(meaning);
		free(target);
	} else {
		e = calloc(1, sizeof(*e));
		e->key = strdup(path);
		e->value = meaning;
		e->value_len = strlen(meaning);
		alligator_ht_insert(map, &e->node, e, tommy_strhash_u32(0, e->key));
		free(target);
	}
	*out = vrl_null();
	return VRL_OK;
}

void vrl_secret_builtins_init(void)
{
	vrl_register("get_secret", fn_get_secret);
	vrl_register("set_secret", fn_set_secret);
	vrl_register("remove_secret", fn_remove_secret);
	vrl_register("set_semantic_meaning", fn_set_semantic_meaning);
}
