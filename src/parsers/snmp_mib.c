#include "parsers/snmp_mib.h"
#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MIB_MAX_ARC 128
#define MIB_MAX_TOK 48
#define MIB_MAX_PENDING 65536
#define MIB_OID_HASH 131071
#define MIB_MAX_FILE (8 * 1024 * 1024)

typedef struct mib_oid_ent {
	struct mib_oid_ent *next;
	char *oid;
	char *sym;
} mib_oid_ent;

typedef struct {
	char *name;
	char *rhs; /* inner of { } */
	int module_identity; /* 1 = MODULE-IDENTITY → reverse OID map for symbol label; 0 = OBJECT IDENTIFIER (resolve only) */
} mib_pending;

static mib_oid_ent *g_oid_hash[MIB_OID_HASH];
static char *g_loaded_dirs;
static int g_loaded;

static void mib_sym_clear(void);

static uint32_t mib_hash_oid(const char *s)
{
	uint32_t h = 5381u;
	for (; *s; s++)
		h = ((h << 5) + h) + (unsigned char)*s;
	return h % MIB_OID_HASH;
}

static mib_oid_ent *mib_oid_find(const char *oid)
{
	for (mib_oid_ent *e = g_oid_hash[mib_hash_oid(oid)]; e; e = e->next)
		if (!strcmp(e->oid, oid))
			return e;
	return NULL;
}

static void mib_oid_insert(const char *oid, const char *sym)
{
	if (!oid || !sym || !oid[0])
		return;
	if (mib_oid_find(oid))
		return; /* first registration wins (stable vs readdir order) */
	mib_oid_ent *e = calloc(1, sizeof *e);
	if (!e)
		return;
	e->oid = strdup(oid);
	e->sym = strdup(sym);
	if (!e->oid || !e->sym) {
		free(e->oid);
		free(e->sym);
		free(e);
		return;
	}
	uint32_t b = mib_hash_oid(oid);
	e->next = g_oid_hash[b];
	g_oid_hash[b] = e;
}

static void mib_clear(void)
{
	mib_sym_clear();
	for (size_t i = 0; i < MIB_OID_HASH; i++) {
		mib_oid_ent *e = g_oid_hash[i];
		while (e) {
			mib_oid_ent *n = e->next;
			free(e->oid);
			free(e->sym);
			free(e);
			e = n;
		}
		g_oid_hash[i] = NULL;
	}
	free(g_loaded_dirs);
	g_loaded_dirs = NULL;
	g_loaded = 0;
}

/* symbol -> arc list for resolution pass */
typedef struct mib_sym_arcs {
	char *name;
	uint32_t arcs[MIB_MAX_ARC];
	size_t n;
	int ok;
	struct mib_sym_arcs *next;
} mib_sym_arcs;

static mib_sym_arcs *g_sym_hash[MIB_OID_HASH];

static uint32_t mib_hash_sym(const char *s)
{
	return mib_hash_oid(s);
}

static mib_sym_arcs *mib_sym_find(const char *name)
{
	for (mib_sym_arcs *e = g_sym_hash[mib_hash_sym(name)]; e; e = e->next)
		if (!strcmp(e->name, name))
			return e;
	return NULL;
}

static void mib_sym_set_arcs(const char *name, const uint32_t *a, size_t n)
{
	if (!name || n == 0 || n > MIB_MAX_ARC)
		return;
	mib_sym_arcs *e = mib_sym_find(name);
	if (!e) {
		e = calloc(1, sizeof *e);
		if (!e)
			return;
		e->name = strdup(name);
		if (!e->name) {
			free(e);
			return;
		}
		uint32_t b = mib_hash_sym(name);
		e->next = g_sym_hash[b];
		g_sym_hash[b] = e;
	}
	memcpy(e->arcs, a, n * sizeof(uint32_t));
	e->n = n;
	e->ok = 1;
}

static void mib_sym_clear(void)
{
	for (size_t i = 0; i < MIB_OID_HASH; i++) {
		mib_sym_arcs *e = g_sym_hash[i];
		while (e) {
			mib_sym_arcs *n = e->next;
			free(e->name);
			free(e);
			e = n;
		}
		g_sym_hash[i] = NULL;
	}
}

static void mib_strip_comments(char *s)
{
	for (char *p = s; *p; p++) {
		if (p[0] == '-' && p[1] == '-') {
			while (*p && *p != '\n')
				*p++ = ' ';
			if (*p == '\n')
				p--;
		}
	}
}

static int mib_brace_inner(const char *p, const char **inner, size_t *ilen)
{
	if (*p != '{')
		return -1;
	int d = 0;
	for (const char *q = p; *q; q++) {
		if (*q == '{')
			d++;
		else if (*q == '}') {
			d--;
			if (d == 0) {
				*inner = p + 1;
				*ilen = (size_t)(q - p - 1);
				return 0;
			}
		}
	}
	return -1;
}

static int mib_is_uint(const char *t)
{
	if (!t || !t[0])
		return 0;
	for (const char *p = t; *p; p++)
		if (!isdigit((unsigned char)*p))
			return 0;
	return 1;
}

/* Parse name(number) -> number, or plain uint, or identifier */
static int mib_tokenize_rhs(const char *inner, size_t ilen, char **toks, int *ntok)
{
	char *buf = malloc(ilen + 1);
	if (!buf)
		return -1;
	memcpy(buf, inner, ilen);
	buf[ilen] = 0;
	*ntok = 0;
	for (char *ctx = NULL, *w = strtok_r(buf, ", \t\r\n", &ctx); w;
	     w = strtok_r(NULL, ", \t\r\n", &ctx)) {
		if (*ntok >= MIB_MAX_TOK) {
			free(buf);
			return -1;
		}
		/* org(3) -> 3 */
		char *lp = strchr(w, '(');
		if (lp && strchr(lp + 1, ')')) {
			lp++;
			char *rp = strchr(lp, ')');
			*rp = 0;
			if (!mib_is_uint(lp)) {
				free(buf);
				return -1;
			}
			toks[*ntok] = strdup(lp);
		} else
			toks[*ntok] = strdup(w);
		if (!toks[*ntok]) {
			for (int i = 0; i < *ntok; i++)
				free(toks[i]);
			free(buf);
			return -1;
		}
		(*ntok)++;
	}
	free(buf);
	return 0;
}

static int mib_eval_tokens(uint32_t *out, size_t *nout, char **toks, int ntok)
{
	*nout = 0;
	for (int i = 0; i < ntok; i++) {
		if (mib_is_uint(toks[i])) {
			if (*nout >= MIB_MAX_ARC)
				return -1;
			out[(*nout)++] = (uint32_t)strtoul(toks[i], NULL, 10);
			continue;
		}
		mib_sym_arcs *se = mib_sym_find(toks[i]);
		if (!se || !se->ok)
			return -1;
		for (size_t j = 0; j < se->n; j++) {
			if (*nout >= MIB_MAX_ARC)
				return -1;
			out[(*nout)++] = se->arcs[j];
		}
	}
	return 0;
}

static void mib_arcs_to_dotted(const uint32_t *a, size_t n, char *buf, size_t cap)
{
	size_t w = 0;
	for (size_t i = 0; i < n && w + 2 < cap; i++)
		w += (size_t)snprintf(buf + w, cap - w, "%s%" PRIu32, i ? "." : "", a[i]);
}

static void mib_seed_iso(void)
{
	uint32_t one = 1;
	mib_sym_set_arcs("iso", &one, 1);
}

static int mib_add_pending(mib_pending **list, size_t *n, size_t *cap, const char *name, const char *rhs,
			 size_t rhslen, int module_identity)
{
	if (*n >= *cap) {
		size_t nc = *cap ? *cap * 2 : 1024;
		if (nc > MIB_MAX_PENDING)
			return -1;
		mib_pending *np = realloc(*list, nc * sizeof **list);
		if (!np)
			return -1;
		*list = np;
		*cap = nc;
	}
	mib_pending *p = &(*list)[*n];
	p->name = strdup(name);
	p->rhs = malloc(rhslen + 1);
	if (!p->name || !p->rhs) {
		free(p->name);
		free(p->rhs);
		return -1;
	}
	memcpy(p->rhs, rhs, rhslen);
	p->rhs[rhslen] = 0;
	p->module_identity = module_identity;
	(*n)++;
	return 0;
}

static void mib_parse_file(char *buf, mib_pending **pend, size_t *pn, size_t *pcap)
{
	/* OBJECT IDENTIFIER ::= on one line */
	for (char *line = buf; line && *line;) {
		char *nl = strchr(line, '\n');
		if (nl)
			*nl = 0;
		char *oi = strstr(line, "OBJECT IDENTIFIER");
		char *as = strstr(line, "::=");
		if (oi && as && oi < as) {
			char *s = line;
			while (*s && isspace((unsigned char)*s))
				s++;
			char *name_end = s;
			while (*name_end && (isalnum((unsigned char)*name_end) || *name_end == '-'))
				name_end++;
			if (name_end > s) {
				size_t name_len = (size_t)(name_end - s);
				char name[256];
				if (name_len < sizeof name) {
					memcpy(name, s, name_len);
					name[name_len] = 0;
					char *lb = strchr(as, '{');
					if (lb) {
						const char *inner;
						size_t il;
						if (!mib_brace_inner(lb, &inner, &il))
							mib_add_pending(pend, pn, pcap, name, inner, il, 0);
					}
				}
			}
		}
		if (nl) {
			*nl = '\n';
			line = nl + 1;
		} else
			break;
	}

	/* MODULE-IDENTITY ... ::= { ... } */
	for (char *p = buf; (p = strstr(p, "MODULE-IDENTITY")) != NULL;) {
		char *line0 = p;
		while (line0 > buf && line0[-1] != '\n')
			line0--;
		while (*line0 && isspace((unsigned char)*line0))
			line0++;
		char *sym_start = line0;
		char *sym_end = sym_start;
		while (*sym_end && (isalnum((unsigned char)*sym_end) || *sym_end == '-'))
			sym_end++;
		if (sym_end == sym_start || sym_end > p) {
			p++;
			continue;
		}
		int gap_bad = 0;
		for (char *q = sym_end; q < p; q++) {
			if (!isspace((unsigned char)*q)) {
				gap_bad = 1;
				break;
			}
		}
		if (gap_bad) {
			p++;
			continue;
		}
		size_t sl = (size_t)(sym_end - sym_start);
		char name[256];
		if (sl == 0 || sl >= sizeof name) {
			p++;
			continue;
		}
		memcpy(name, sym_start, sl);
		name[sl] = 0;
		char *as = strstr(p, "::=");
		if (!as) {
			p++;
			continue;
		}
		char *lb = strchr(as, '{');
		if (!lb) {
			p = as + 3;
			continue;
		}
		const char *inner;
		size_t il;
		if (mib_brace_inner(lb, &inner, &il)) {
			p = lb + 1;
			continue;
		}
		mib_add_pending(pend, pn, pcap, name, inner, il, 1);
		p = lb + il + 2;
	}
}

static int mib_resolve_pending(mib_pending *pend, size_t pn)
{
	mib_seed_iso();
	for (int round = 0; round < 500; round++) {
		int prog = 0;
		for (size_t i = 0; i < pn; i++) {
			mib_sym_arcs *have = mib_sym_find(pend[i].name);
			if (have && have->ok)
				continue;
			char *toks[MIB_MAX_TOK];
			int ntok = 0;
			if (mib_tokenize_rhs(pend[i].rhs, strlen(pend[i].rhs), toks, &ntok))
				continue;
			uint32_t arcs[MIB_MAX_ARC];
			size_t na;
			if (mib_eval_tokens(arcs, &na, toks, ntok) != 0) {
				for (int t = 0; t < ntok; t++)
					free(toks[t]);
				continue;
			}
			for (int t = 0; t < ntok; t++)
				free(toks[t]);
			mib_sym_set_arcs(pend[i].name, arcs, na);
			if (pend[i].module_identity) {
				char dotted[512];
				mib_arcs_to_dotted(arcs, na, dotted, sizeof dotted);
				mib_oid_insert(dotted, pend[i].name);
			}
			prog = 1;
		}
		if (!prog)
			break;
	}
	return 0;
}

/*
 * sysORID often carries historical mib-2 subtree roots. MODULE-IDENTITY in current MIBs may use a different
 * registration (e.g. RFC 4293 ipMIB ::= { mib-2 48 } while agents still report 1.3.6.1.2.1.4).
 * mib_oid_insert skips if the OID is already present.
 */
static void mib_register_legacy_sysor_aliases(void)
{
	static const struct {
		const char *oid;
		const char *sym;
	} tab[] = {
		{ "1.3.6.1.2.1.4", "ipMIB" },
	};
	for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
		mib_oid_insert(tab[i].oid, tab[i].sym);
}

static void mib_free_pending(mib_pending *pend, size_t pn)
{
	for (size_t i = 0; i < pn; i++) {
		free(pend[i].name);
		free(pend[i].rhs);
	}
	free(pend);
}

static int mib_name_ends_ci(const char *name, const char *suf)
{
	size_t nl = strlen(name), ns = strlen(suf);
	if (nl < ns)
		return 0;
	const char *a = name + nl - ns;
	for (size_t i = 0; i < ns; i++) {
		if (tolower((unsigned char)a[i]) != tolower((unsigned char)suf[i]))
			return 0;
	}
	return 1;
}

static int mib_read_file(const char *path, char **out, size_t *osz)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return -1;
	if (fseek(f, 0, SEEK_END))
		goto bad;
	long sz = ftell(f);
	if (sz < 0 || sz > MIB_MAX_FILE)
		goto bad;
	rewind(f);
	*out = malloc((size_t)sz + 1);
	if (!*out)
		goto bad;
	if (fread(*out, 1, (size_t)sz, f) != (size_t)sz)
		goto bad2;
	(*out)[sz] = 0;
	fclose(f);
	*osz = (size_t)sz;
	return 0;
bad2:
	free(*out);
	*out = NULL;
bad:
	fclose(f);
	return -1;
}

static void mib_load_dir(const char *dir, mib_pending **pend, size_t *pn, size_t *pcap)
{
	DIR *d = opendir(dir);
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		if (de->d_name[0] == '.')
			continue;
		int ok = mib_name_ends_ci(de->d_name, ".txt") || mib_name_ends_ci(de->d_name, ".mib");
		if (!ok)
			continue;
		char path[4096];
		if (snprintf(path, sizeof path, "%s/%s", dir, de->d_name) >= (int)sizeof path)
			continue;
		struct stat st;
		if (stat(path, &st) || !S_ISREG(st.st_mode))
			continue;
		char *buf = NULL;
		size_t bz = 0;
		if (mib_read_file(path, &buf, &bz))
			continue;
		mib_strip_comments(buf);
		mib_parse_file(buf, pend, pn, pcap);
		free(buf);
	}
	closedir(d);
}

size_t snmp_mib_oid_map_size(void)
{
	size_t n = 0;
	for (size_t i = 0; i < MIB_OID_HASH; i++)
		for (mib_oid_ent *e = g_oid_hash[i]; e; e = e->next)
			n++;
	return n;
}

int snmp_mib_ensure_loaded(const char *colon_dirs)
{
	if (!colon_dirs || !colon_dirs[0])
		return 0;
	if (g_loaded && g_loaded_dirs && !strcmp(g_loaded_dirs, colon_dirs))
		return 0;
	mib_clear();
	g_loaded_dirs = strdup(colon_dirs);
	if (!g_loaded_dirs)
		return 0;

	mib_pending *pend = NULL;
	size_t pn = 0, pcap = 0;
	char *dirs_copy = strdup(colon_dirs);
	if (!dirs_copy) {
		free(g_loaded_dirs);
		g_loaded_dirs = NULL;
		return 0;
	}
	for (char *dir = dirs_copy, *brk = NULL; (dir = strtok_r(dir, ":", &brk)); dir = NULL) {
		while (*dir && isspace((unsigned char)*dir))
			dir++;
		if (*dir)
			mib_load_dir(dir, &pend, &pn, &pcap);
	}
	free(dirs_copy);

	mib_resolve_pending(pend, pn);
	mib_free_pending(pend, pn);
	mib_register_legacy_sysor_aliases();
	g_loaded = 1;
	return 1;
}

void snmp_mib_lookup_symbol(const char *dotted_oid, char *out, size_t outcap)
{
	out[0] = 0;
	if (!dotted_oid || !dotted_oid[0] || outcap == 0)
		return;
	char buf[512];
	snprintf(buf, sizeof buf, "%s", dotted_oid);
	for (;;) {
		mib_oid_ent *e = mib_oid_find(buf);
		if (e) {
			strlcpy(out, e->sym, outcap);
			return;
		}
		char *dot = strrchr(buf, '.');
		if (!dot)
			break;
		*dot = 0;
		if (!buf[0])
			break;
	}
}
