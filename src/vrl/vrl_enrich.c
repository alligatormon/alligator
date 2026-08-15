/*
 * Alligator host builtins: enrichment tables (Vector-compatible subset).
 *
 *   get_enrichment_table_record(table, condition, [select], [case_sensitive])
 *   find_enrichment_table_records(table, condition, [select], [case_sensitive])
 *
 * Config:
 *   enrichment_table { name city; type mmdb; path /path/GeoLite2-City.mmdb; }
 *   enrichment_table { name codes; type file; path /etc/alligator/codes.csv; }
 *
 * Types: file (CSV), mmdb / geoip (MaxMind DB via libmaxminddb).
 */

#define _GNU_SOURCE
#include "vrl/type.h"
#include "common/logs.h"
#include "common/selector.h"
#include "main.h"
#include <ctype.h>
#include <maxminddb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern aconf *ac;

typedef enum {
	VRL_ENRICH_FILE = 0,
	VRL_ENRICH_MMDB = 1,
} vrl_enrich_type;

typedef struct {
	char **cols; /* owned header names */
	size_t ncols;
	char ***rows; /* owned [nrows][ncols] cell strings */
	size_t nrows;
} vrl_enrich_csv;

typedef struct vrl_enrich_table {
	alligator_ht_node node;
	char *name;
	char *path;
	vrl_enrich_type type;
	vrl_enrich_csv csv;
	MMDB_s mmdb;
	int mmdb_open;
} vrl_enrich_table;

static int vrl_enrich_compare(const void *arg, const void *obj)
{
	return strcmp((const char *)arg, ((const vrl_enrich_table *)obj)->name);
}

static void vrl_enrich_csv_free(vrl_enrich_csv *c)
{
	if (!c)
		return;
	for (size_t i = 0; i < c->ncols; i++)
		free(c->cols[i]);
	free(c->cols);
	for (size_t r = 0; r < c->nrows; r++) {
		for (size_t i = 0; i < c->ncols; i++)
			free(c->rows[r][i]);
		free(c->rows[r]);
	}
	free(c->rows);
	memset(c, 0, sizeof(*c));
}

static void vrl_enrich_table_free(void *arg)
{
	vrl_enrich_table *t = arg;
	if (!t)
		return;
	free(t->name);
	free(t->path);
	vrl_enrich_csv_free(&t->csv);
	if (t->mmdb_open) {
		MMDB_close(&t->mmdb);
		t->mmdb_open = 0;
	}
	free(t);
}

static char *vrl_enrich_trim_dup(const char *s, size_t n)
{
	while (n && isspace((unsigned char)*s)) {
		s++;
		n--;
	}
	while (n && isspace((unsigned char)s[n - 1]))
		n--;
	char *out = malloc(n + 1);
	if (!out)
		return NULL;
	memcpy(out, s, n);
	out[n] = 0;
	return out;
}

/* Split a CSV line on commas (no quoted-field support in v1). */
static size_t vrl_enrich_split_csv_line(char *line, char ***out_fields)
{
	size_t n = 0, cap = 8;
	char **fields = calloc(cap, sizeof(char *));
	char *p = line;
	while (p) {
		char *comma = strchr(p, ',');
		size_t len = comma ? (size_t)(comma - p) : strlen(p);
		if (n == cap) {
			cap *= 2;
			fields = realloc(fields, cap * sizeof(char *));
		}
		fields[n++] = vrl_enrich_trim_dup(p, len);
		if (!comma)
			break;
		p = comma + 1;
	}
	*out_fields = fields;
	return n;
}

static int vrl_enrich_load_csv(vrl_enrich_table *t)
{
	FILE *f = fopen(t->path, "r");
	if (!f) {
		glog(L_ERROR, "enrichment_table '%s': cannot open CSV '%s'\n", t->name, t->path);
		return 0;
	}
	char *line = NULL;
	size_t linesz = 0;
	ssize_t nread = getline(&line, &linesz, f);
	if (nread <= 0) {
		free(line);
		fclose(f);
		glog(L_ERROR, "enrichment_table '%s': empty CSV '%s'\n", t->name, t->path);
		return 0;
	}
	if (line[nread - 1] == '\n')
		line[--nread] = 0;
	if (nread > 0 && line[nread - 1] == '\r')
		line[--nread] = 0;

	char **hdr = NULL;
	size_t ncols = vrl_enrich_split_csv_line(line, &hdr);
	t->csv.cols = hdr;
	t->csv.ncols = ncols;

	size_t rcap = 64;
	t->csv.rows = calloc(rcap, sizeof(char **));
	t->csv.nrows = 0;

	while ((nread = getline(&line, &linesz, f)) > 0) {
		if (line[nread - 1] == '\n')
			line[--nread] = 0;
		if (nread > 0 && line[nread - 1] == '\r')
			line[--nread] = 0;
		if (!nread)
			continue;
		char **cells = NULL;
		size_t nc = vrl_enrich_split_csv_line(line, &cells);
		/* Pad / truncate to header width. */
		char **row = calloc(ncols, sizeof(char *));
		for (size_t i = 0; i < ncols; i++) {
			if (i < nc && cells[i])
				row[i] = cells[i];
			else
				row[i] = strdup("");
		}
		for (size_t i = ncols; i < nc; i++)
			free(cells[i]);
		free(cells);
		if (t->csv.nrows == rcap) {
			rcap *= 2;
			t->csv.rows = realloc(t->csv.rows, rcap * sizeof(char **));
		}
		t->csv.rows[t->csv.nrows++] = row;
	}
	free(line);
	fclose(f);
	glog(L_INFO, "enrichment_table '%s': loaded CSV '%s' (%zu cols, %zu rows)\n",
	     t->name, t->path, t->csv.ncols, t->csv.nrows);
	return 1;
}

static int vrl_enrich_load_mmdb(vrl_enrich_table *t)
{
	int status = MMDB_open(t->path, MMDB_MODE_MMAP, &t->mmdb);
	if (status != MMDB_SUCCESS) {
		glog(L_ERROR, "enrichment_table '%s': MMDB_open('%s') failed: %s\n",
		     t->name, t->path, MMDB_strerror(status));
		return 0;
	}
	t->mmdb_open = 1;
	glog(L_INFO, "enrichment_table '%s': opened MMDB '%s'\n", t->name, t->path);
	return 1;
}

static vrl_enrich_table *vrl_enrich_get(const char *name)
{
	if (!ac || !ac->enrichment_tables || !name)
		return NULL;
	return alligator_ht_search(ac->enrichment_tables, vrl_enrich_compare, name,
				   tommy_strhash_u32(0, (char *)name));
}

int vrl_enrich_push_json(json_t *cfg)
{
	if (!cfg || !ac)
		return 0;
	if (!ac->enrichment_tables)
		ac->enrichment_tables = alligator_ht_init(NULL);

	json_t *jname = json_object_get(cfg, "name");
	json_t *jtype = json_object_get(cfg, "type");
	json_t *jpath = json_object_get(cfg, "path");
	if (!jname || !jtype || !jpath ||
	    json_typeof(jname) != JSON_STRING ||
	    json_typeof(jtype) != JSON_STRING ||
	    json_typeof(jpath) != JSON_STRING) {
		glog(L_ERROR, "enrichment_table: need string fields name, type, path\n");
		return 0;
	}

	const char *name = json_string_value(jname);
	const char *type = json_string_value(jtype);
	const char *path = json_string_value(jpath);

	vrl_enrich_table *existing = vrl_enrich_get(name);
	if (existing) {
		alligator_ht_remove_existing(ac->enrichment_tables, &existing->node);
		vrl_enrich_table_free(existing);
	}

	vrl_enrich_table *t = calloc(1, sizeof(*t));
	t->name = strdup(name);
	t->path = strdup(path);
	if (!strcmp(type, "file") || !strcmp(type, "csv"))
		t->type = VRL_ENRICH_FILE;
	else if (!strcmp(type, "mmdb") || !strcmp(type, "geoip"))
		t->type = VRL_ENRICH_MMDB;
	else {
		glog(L_ERROR, "enrichment_table '%s': unknown type '%s' (file|mmdb|geoip)\n",
		     name, type);
		vrl_enrich_table_free(t);
		return 0;
	}

	int ok = (t->type == VRL_ENRICH_FILE) ? vrl_enrich_load_csv(t) : vrl_enrich_load_mmdb(t);
	if (!ok) {
		vrl_enrich_table_free(t);
		return 0;
	}

	alligator_ht_insert(ac->enrichment_tables, &t->node, t, tommy_strhash_u32(0, t->name));
	return 1;
}

int vrl_enrich_del_json(json_t *cfg)
{
	json_t *jname = cfg ? json_object_get(cfg, "name") : NULL;
	if (!jname || json_typeof(jname) != JSON_STRING)
		return 0;
	const char *name = json_string_value(jname);
	vrl_enrich_table *t = vrl_enrich_get(name);
	if (!t)
		return 0;
	alligator_ht_remove_existing(ac->enrichment_tables, &t->node);
	vrl_enrich_table_free(t);
	return 1;
}

static void vrl_enrich_free_foreach(void *funcarg, void *arg)
{
	(void)funcarg;
	vrl_enrich_table_free(arg);
}

void vrl_enrich_tables_free(void)
{
	if (!ac || !ac->enrichment_tables)
		return;
	alligator_ht_foreach_arg(ac->enrichment_tables, vrl_enrich_free_foreach, NULL);
	alligator_ht_done(ac->enrichment_tables);
	free(ac->enrichment_tables);
	ac->enrichment_tables = NULL;
}

void vrl_enrich_generate_conf(void *funcarg, void *arg)
{
	json_t *dst = funcarg;
	vrl_enrich_table *t = arg;
	if (!dst || !t)
		return;

	json_t *arr = json_object_get(dst, "enrichment_table");
	if (!arr) {
		arr = json_array();
		json_array_object_insert(dst, "enrichment_table", arr);
	}
	json_t *ctx = json_object();
	json_array_object_insert(arr, NULL, ctx);
	json_array_object_insert(ctx, "name", json_string(t->name));
	json_array_object_insert(ctx, "type",
				 json_string(t->type == VRL_ENRICH_FILE ? "file" : "mmdb"));
	json_array_object_insert(ctx, "path", json_string(t->path));
}

static char *vrl_value_as_cstr(vrl_value *v)
{
	if (!v)
		return NULL;
	if (v->type == VRL_BYTES && v->u.bytes.data)
		return strndup(v->u.bytes.data, v->u.bytes.len);
	if (v->type == VRL_INTEGER) {
		char buf[64];
		snprintf(buf, sizeof(buf), "%lld", (long long)v->u.integer);
		return strdup(buf);
	}
	if (v->type == VRL_FLOAT) {
		char buf[64];
		snprintf(buf, sizeof(buf), "%g", v->u.flt);
		return strdup(buf);
	}
	if (v->type == VRL_BOOLEAN)
		return strdup(v->u.boolean ? "true" : "false");
	return NULL;
}

static int vrl_cstr_eq(const char *a, const char *b, int case_sensitive)
{
	if (!a || !b)
		return 0;
	return case_sensitive ? !strcmp(a, b) : !strcasecmp(a, b);
}

static int vrl_enrich_select_wants(vrl_value *select, const char *field)
{
	if (!select || select->type != VRL_ARRAY)
		return 1; /* all fields */
	size_t n = vrl_array_len(select);
	for (size_t i = 0; i < n; i++) {
		vrl_value *e = vrl_array_get(select, i);
		if (e && e->type == VRL_BYTES && e->u.bytes.data &&
		    e->u.bytes.len == strlen(field) &&
		    !memcmp(e->u.bytes.data, field, e->u.bytes.len))
			return 1;
	}
	return 0;
}

static vrl_value *vrl_enrich_row_to_object(vrl_enrich_table *t, size_t row,
					   vrl_value *select)
{
	vrl_value *obj = vrl_object_new();
	for (size_t i = 0; i < t->csv.ncols; i++) {
		if (!vrl_enrich_select_wants(select, t->csv.cols[i]))
			continue;
		const char *cell = t->csv.rows[row][i] ? t->csv.rows[row][i] : "";
		vrl_object_set_cstr(obj, t->csv.cols[i], vrl_bytes(cell, strlen(cell)));
	}
	return obj;
}

static int vrl_enrich_csv_match_row(vrl_enrich_table *t, size_t row, vrl_value *cond,
				    int case_sensitive)
{
	if (!cond || cond->type != VRL_OBJECT)
		return 0;
	for (size_t i = 0; i < cond->u.object.len; i++) {
		vrl_object_entry *e = &cond->u.object.entries[i];
		ssize_t col = -1;
		for (size_t c = 0; c < t->csv.ncols; c++) {
			if (strlen(t->csv.cols[c]) == e->key_len &&
			    !memcmp(t->csv.cols[c], e->key, e->key_len)) {
				col = (ssize_t)c;
				break;
			}
		}
		if (col < 0)
			return 0;
		char *want = vrl_value_as_cstr(e->val);
		int ok = vrl_cstr_eq(t->csv.rows[row][col], want, case_sensitive);
		free(want);
		if (!ok)
			return 0;
	}
	return 1;
}

static void vrl_enrich_mmdb_put_utf8(vrl_value *obj, const char *key, MMDB_entry_data_s *d)
{
	if (!d || !d->has_data || d->type != MMDB_DATA_TYPE_UTF8_STRING || !d->utf8_string)
		vrl_object_set_cstr(obj, key, vrl_null());
	else
		vrl_object_set_cstr(obj, key, vrl_bytes(d->utf8_string, d->data_size));
}

static void vrl_enrich_mmdb_put_double(vrl_value *obj, const char *key, MMDB_entry_data_s *d)
{
	if (!d || !d->has_data) {
		vrl_object_set_cstr(obj, key, vrl_null());
		return;
	}
	double v = 0;
	if (d->type == MMDB_DATA_TYPE_DOUBLE)
		v = d->double_value;
	else if (d->type == MMDB_DATA_TYPE_FLOAT)
		v = d->float_value;
	else {
		vrl_object_set_cstr(obj, key, vrl_null());
		return;
	}
	vrl_object_set_cstr(obj, key, vrl_float(v));
}

static vrl_value *vrl_enrich_mmdb_lookup(vrl_enrich_table *t, const char *ip, vrl_value *select)
{
	int gai_error = 0, mmdb_error = 0;
	MMDB_lookup_result_s res =
		MMDB_lookup_string(&t->mmdb, ip, &gai_error, &mmdb_error);
	if (gai_error || mmdb_error != MMDB_SUCCESS || !res.found_entry)
		return NULL;

	vrl_value *obj = vrl_object_new();
	MMDB_entry_data_s d;

	struct {
		const char *out;
		const char *path[5];
		int is_double;
	} fields[] = {
		{ "country_code", { "country", "iso_code", NULL }, 0 },
		{ "country_name", { "country", "names", "en", NULL }, 0 },
		{ "city_name", { "city", "names", "en", NULL }, 0 },
		{ "continent_code", { "continent", "code", NULL }, 0 },
		{ "postal_code", { "postal", "code", NULL }, 0 },
		{ "timezone", { "location", "time_zone", NULL }, 0 },
		{ "latitude", { "location", "latitude", NULL }, 1 },
		{ "longitude", { "location", "longitude", NULL }, 1 },
		{ "region_code", { "subdivisions", "0", "iso_code", NULL }, 0 },
		{ "region_name", { "subdivisions", "0", "names", "en", NULL }, 0 },
	};

	for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
		if (!vrl_enrich_select_wants(select, fields[i].out))
			continue;
		int st = MMDB_aget_value(&res.entry, &d, fields[i].path);
		if (st != MMDB_SUCCESS)
			memset(&d, 0, sizeof(d));
		if (fields[i].is_double)
			vrl_enrich_mmdb_put_double(obj, fields[i].out, &d);
		else
			vrl_enrich_mmdb_put_utf8(obj, fields[i].out, &d);
	}
	return obj;
}

static vrl_status vrl_enrich_query(vrl_call_args *a, int want_many, vrl_value **out, char **err)
{
	vrl_value *table_v = vrl_arg(a, "table", 0);
	vrl_value *cond = vrl_arg(a, "condition", 1);
	vrl_value *select = vrl_arg(a, "select", 2);
	vrl_value *cs_v = vrl_arg(a, "case_sensitive", 3);

	if (!table_v || table_v->type != VRL_BYTES || !table_v->u.bytes.data) {
		*err = vrl_errf("enrichment: 'table' must be a string");
		return VRL_ERR;
	}
	if (!cond || cond->type != VRL_OBJECT) {
		*err = vrl_errf("enrichment: 'condition' must be an object");
		return VRL_ERR;
	}

	char *tname = strndup(table_v->u.bytes.data, table_v->u.bytes.len);
	vrl_enrich_table *t = vrl_enrich_get(tname);
	free(tname);
	if (!t) {
		*err = vrl_errf("enrichment table not found");
		return VRL_ERR;
	}

	int case_sensitive = 1;
	if (cs_v && cs_v->type == VRL_BOOLEAN)
		case_sensitive = cs_v->u.boolean;

	if (t->type == VRL_ENRICH_FILE) {
		vrl_value *matches = want_many ? vrl_array_new() : NULL;
		size_t found = 0;
		vrl_value *one = NULL;
		for (size_t r = 0; r < t->csv.nrows; r++) {
			if (!vrl_enrich_csv_match_row(t, r, cond, case_sensitive))
				continue;
			vrl_value *row = vrl_enrich_row_to_object(t, r, select);
			found++;
			if (want_many)
				vrl_array_push(matches, row);
			else {
				if (found > 1) {
					vrl_value_unref(row);
					vrl_value_unref(one);
					*err = vrl_errf("enrichment: more than one matching row");
					return VRL_ERR;
				}
				one = row;
			}
		}
		if (want_many) {
			*out = matches;
			return VRL_OK;
		}
		if (!one) {
			*err = vrl_errf("enrichment: no matching row");
			return VRL_ERR;
		}
		*out = one;
		return VRL_OK;
	}

	/* MMDB / geoip: condition must include "ip". */
	vrl_value *ip_v = vrl_object_get(cond, "ip", 2);
	if (!ip_v || ip_v->type != VRL_BYTES || !ip_v->u.bytes.data) {
		*err = vrl_errf("enrichment mmdb: condition needs string field 'ip'");
		return VRL_ERR;
	}
	char *ip = strndup(ip_v->u.bytes.data, ip_v->u.bytes.len);
	vrl_value *rec = vrl_enrich_mmdb_lookup(t, ip, select);
	free(ip);
	if (!rec) {
		*err = vrl_errf("enrichment: no matching geoip/mmdb record");
		return VRL_ERR;
	}
	if (want_many) {
		vrl_value *arr = vrl_array_new();
		vrl_array_push(arr, rec);
		*out = arr;
	} else {
		*out = rec;
	}
	return VRL_OK;
}

static vrl_status fn_get_enrichment_table_record(vrl_call_args *a, vrl_value **out, char **err)
{
	return vrl_enrich_query(a, 0, out, err);
}

static vrl_status fn_find_enrichment_table_records(vrl_call_args *a, vrl_value **out, char **err)
{
	return vrl_enrich_query(a, 1, out, err);
}

void vrl_enrich_builtins_init(void)
{
	vrl_register("get_enrichment_table_record", fn_get_enrichment_table_record);
	vrl_register("find_enrichment_table_records", fn_find_enrichment_table_records);
}
