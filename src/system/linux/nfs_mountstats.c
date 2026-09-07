#ifdef __linux__

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "main.h"
#include "common/logs.h"
#include "system/linux/nfs_mountstats.h"

extern aconf *ac;

#define NFS_LINE_MAX 4096
#define NFS_XPRT_MAX 4
#define NFS_EVENTS_LEN 27
#define NFS_BYTES_LEN 8

struct nfs_xprt_acc {
	char protocol[8];
	uint64_t bind;
	uint64_t connect;
	uint64_t sends;
	uint64_t recvs;
	uint64_t bad_xid;
	uint64_t backlog;
	uint64_t sending_q;
	uint64_t pending_q;
	uint64_t idle;
	uint64_t max_slots;
	int have_idle;
	int have_slots;
	int used;
};

static char *nfs_byte_names[NFS_BYTES_LEN] = {
	"read", "write", "direct_read", "direct_write",
	"server_read", "server_write", "read_pages", "write_pages"
};

static char *nfs_event_names[NFS_EVENTS_LEN] = {
	"inode_revalidate", "dnode_revalidate", "data_invalidate", "attribute_invalidate",
	"vfs_open", "vfs_lookup", "vfs_access", "vfs_update_page",
	"vfs_read_page", "vfs_read_pages", "vfs_write_page", "vfs_write_pages",
	"vfs_getdents", "vfs_setattr", "vfs_flush", "vfs_fsync",
	"vfs_lock", "vfs_file_release", "congestion_wait", "truncation",
	"write_extension", "silly_rename", "short_read", "short_write",
	"jukebox_delay", "pnfs_read", "pnfs_write"
};

static int nfs_op_wanted(const char *op)
{
	static const char *want[] = {
		"READ", "WRITE", "GETATTR", "LOOKUP", "ACCESS",
		"READDIR", "READDIRPLUS", "COMMIT", NULL
	};
	size_t i;

	for (i = 0; want[i]; i++) {
		if (!strcmp(op, want[i]))
			return 1;
	}
	return 0;
}

static int nfs_is_nfs_fstype(const char *fstype)
{
	return !strcmp(fstype, "nfs") || !strcmp(fstype, "nfs4");
}

static int nfs_parse_device_line(const char *line, char *device, size_t dsz,
	char *mount, size_t msz, char *fstype, size_t fsz)
{
	const char *p = line;
	const char *mounted;
	const char *with;
	size_t dlen;
	size_t mlen;
	size_t flen;

	p += strspn(p, " \t");
	if (strncmp(p, "device ", 7))
		return 0;
	p += 7;
	mounted = strstr(p, " mounted on ");
	if (!mounted)
		return 0;
	dlen = (size_t)(mounted - p);
	if (!dlen || dlen >= dsz)
		return 0;
	memcpy(device, p, dlen);
	device[dlen] = '\0';

	p = mounted + strlen(" mounted on ");
	with = strstr(p, " with fstype ");
	if (!with)
		return 0;
	mlen = (size_t)(with - p);
	if (!mlen || mlen >= msz)
		return 0;
	memcpy(mount, p, mlen);
	mount[mlen] = '\0';

	p = with + strlen(" with fstype ");
	p += strspn(p, " \t");
	flen = strcspn(p, " \t\n\r");
	if (!flen || flen >= fsz)
		return 0;
	memcpy(fstype, p, flen);
	fstype[flen] = '\0';
	return 1;
}

static int nfs_parse_uints(const char *line, uint64_t *out, int max)
{
	const char *p = line;
	int n = 0;
	char *end;

	while (*p && n < max) {
		p += strspn(p, " \t");
		if (!*p || *p == '\n' || *p == '\r')
			break;
		if (!isdigit((unsigned char)*p)) {
			p += strcspn(p, " \t\n\r");
			continue;
		}
		end = NULL;
		out[n++] = strtoull(p, &end, 10);
		if (end == p)
			break;
		p = end;
	}
	return n;
}

static struct nfs_xprt_acc *nfs_xprt_find(struct nfs_xprt_acc *xs, int *n, const char *proto)
{
	int i;

	for (i = 0; i < *n; i++) {
		if (!strcmp(xs[i].protocol, proto))
			return &xs[i];
	}
	if (*n >= NFS_XPRT_MAX)
		return NULL;
	memset(&xs[*n], 0, sizeof(xs[*n]));
	strlcpy(xs[*n].protocol, proto, sizeof(xs[*n].protocol));
	xs[*n].used = 1;
	return &xs[(*n)++];
}

static void nfs_emit_uint3(char *metric, uint64_t val, char *export, char *mount, char *type)
{
	metric_add_labels3(metric, &val, DATATYPE_UINT, ac->system_carg,
		"export", export, "mountpoint", mount, "type", type);
}

static void nfs_emit_uint4(char *metric, uint64_t val, char *export, char *mount,
	char *a, char *av, char *b, char *bv)
{
	metric_add_labels4(metric, &val, DATATYPE_UINT, ac->system_carg,
		"export", export, "mountpoint", mount, a, av, b, bv);
}

static void nfs_emit_xprt(char *export, char *mount, struct nfs_xprt_acc *xs, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		struct nfs_xprt_acc *x = &xs[i];
		uint64_t info = 1;

		if (!x->used)
			continue;
		metric_add_labels3("nfs_mount_info", &info, DATATYPE_UINT, ac->system_carg,
			"export", export, "mountpoint", mount, "protocol", x->protocol);
		nfs_emit_uint4("nfs_mount_transport_total", x->bind, export, mount,
			"protocol", x->protocol, "type", "bind");
		nfs_emit_uint4("nfs_mount_transport_total", x->connect, export, mount,
			"protocol", x->protocol, "type", "connect");
		nfs_emit_uint4("nfs_mount_transport_total", x->sends, export, mount,
			"protocol", x->protocol, "type", "sends");
		nfs_emit_uint4("nfs_mount_transport_total", x->recvs, export, mount,
			"protocol", x->protocol, "type", "receives");
		nfs_emit_uint4("nfs_mount_transport_total", x->bad_xid, export, mount,
			"protocol", x->protocol, "type", "bad_xid");
		nfs_emit_uint4("nfs_mount_transport_total", x->backlog, export, mount,
			"protocol", x->protocol, "type", "backlog");
		if (x->have_slots) {
			nfs_emit_uint4("nfs_mount_transport_total", x->sending_q, export, mount,
				"protocol", x->protocol, "type", "sending_queue");
			nfs_emit_uint4("nfs_mount_transport_total", x->pending_q, export, mount,
				"protocol", x->protocol, "type", "pending_queue");
			metric_add_labels3("nfs_mount_transport_max_slots", &x->max_slots, DATATYPE_UINT,
				ac->system_carg, "export", export, "mountpoint", mount,
				"protocol", x->protocol);
		}
		if (x->have_idle) {
			metric_add_labels3("nfs_mount_transport_idle_seconds", &x->idle, DATATYPE_UINT,
				ac->system_carg, "export", export, "mountpoint", mount,
				"protocol", x->protocol);
		}
	}
}

static void nfs_parse_xprt(const char *line, struct nfs_xprt_acc *xs, int *nx)
{
	const char *p = line;
	char proto[8];
	uint64_t fields[32];
	int n;
	int i;
	struct nfs_xprt_acc *acc;

	p += strspn(p, " \t");
	if (strncmp(p, "xprt:", 5))
		return;
	p += 5;
	p += strspn(p, " \t");
	n = (int)strcspn(p, " \t\n\r");
	if (n <= 0 || (size_t)n >= sizeof(proto))
		return;
	memcpy(proto, p, (size_t)n);
	proto[n] = '\0';
	p += n;

	n = nfs_parse_uints(p, fields, 32);
	if (n < 7)
		return;

	/* UDP omits connect / connect_idle / idle (fields 3–5 in the TCP layout). */
	if (!strcmp(proto, "udp") && n <= 10) {
		for (i = n - 1; i >= 2; i--)
			fields[i + 3] = fields[i];
		fields[2] = 0;
		fields[3] = 0;
		fields[4] = 0;
		n += 3;
	}

	acc = nfs_xprt_find(xs, nx, proto);
	if (!acc)
		return;

	acc->bind += fields[1];
	acc->connect += fields[2];
	acc->sends += fields[5];
	acc->recvs += fields[6];
	if (n > 7)
		acc->bad_xid += fields[7];
	if (n > 9)
		acc->backlog += fields[9];
	if (!strcmp(proto, "tcp") || !strcmp(proto, "rdma")) {
		if (!acc->have_idle || fields[4] < acc->idle)
			acc->idle = fields[4];
		acc->have_idle = 1;
	}
	if (n > 12) {
		if (!acc->have_slots || fields[10] > acc->max_slots)
			acc->max_slots = fields[10];
		acc->sending_q += fields[11];
		acc->pending_q += fields[12];
		acc->have_slots = 1;
	}
}

static void nfs_parse_op(const char *line, char *export, char *mount)
{
	const char *p = line;
	char op[64];
	size_t oplen;
	uint64_t v[9];
	int n;
	double seconds;

	p += strspn(p, " \t");
	if (!*p || *p == '\n')
		return;
	if (!strncmp(p, "per-op", 6) || !strncmp(p, "device ", 7))
		return;

	oplen = strcspn(p, " \t:");
	if (!oplen || oplen >= sizeof(op))
		return;
	memcpy(op, p, oplen);
	op[oplen] = '\0';
	if (!nfs_op_wanted(op))
		return;

	p += oplen;
	p += strspn(p, " \t:");
	n = nfs_parse_uints(p, v, 9);
	if (n < 8)
		return;

	nfs_emit_uint4("nfs_mount_ops_total", v[0], export, mount, "operation", op, "type", "ops");
	nfs_emit_uint4("nfs_mount_ops_total", v[1], export, mount, "operation", op, "type", "trans");
	nfs_emit_uint4("nfs_mount_ops_total", v[2], export, mount, "operation", op, "type", "timeouts");
	nfs_emit_uint4("nfs_mount_ops_total", v[3], export, mount, "operation", op, "type", "bytes_sent");
	nfs_emit_uint4("nfs_mount_ops_total", v[4], export, mount, "operation", op, "type", "bytes_recv");
	if (n > 8)
		nfs_emit_uint4("nfs_mount_ops_total", v[8], export, mount, "operation", op, "type", "errors");

	seconds = v[5] / 1000.0;
	metric_add_labels4("nfs_mount_ops_seconds_total", &seconds, DATATYPE_DOUBLE, ac->system_carg,
		"export", export, "mountpoint", mount, "operation", op, "type", "queue");
	seconds = v[6] / 1000.0;
	metric_add_labels4("nfs_mount_ops_seconds_total", &seconds, DATATYPE_DOUBLE, ac->system_carg,
		"export", export, "mountpoint", mount, "operation", op, "type", "rtt");
	seconds = v[7] / 1000.0;
	metric_add_labels4("nfs_mount_ops_seconds_total", &seconds, DATATYPE_DOUBLE, ac->system_carg,
		"export", export, "mountpoint", mount, "operation", op, "type", "exe");
}

static void nfs_parse_bytes(const char *line, char *export, char *mount)
{
	uint64_t v[NFS_BYTES_LEN];
	int n = nfs_parse_uints(line, v, NFS_BYTES_LEN);
	int i;

	for (i = 0; i < n && i < NFS_BYTES_LEN; i++)
		nfs_emit_uint3("nfs_mount_bytes_total", v[i], export, mount, nfs_byte_names[i]);
}

static void nfs_parse_events(const char *line, char *export, char *mount)
{
	uint64_t v[NFS_EVENTS_LEN];
	int n = nfs_parse_uints(line, v, NFS_EVENTS_LEN);
	int i;

	for (i = 0; i < n && i < NFS_EVENTS_LEN; i++)
		nfs_emit_uint3("nfs_mount_event_total", v[i], export, mount, nfs_event_names[i]);
}

void get_nfs_mountstats(void)
{
	char path[512];
	FILE *fd;
	char line[NFS_LINE_MAX];
	char export[512];
	char mount[512];
	int in_nfs = 0;
	int in_perop = 0;
	struct nfs_xprt_acc xprt[NFS_XPRT_MAX];
	int nxprt = 0;

	snprintf(path, sizeof(path), "%s/self/mountstats", ac->system_procfs);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: nfs: mountstats '%s'\n", path);

	fd = fopen(path, "r");
	if (!fd)
		return;

	export[0] = '\0';
	mount[0] = '\0';
	memset(xprt, 0, sizeof(xprt));

	while (fgets(line, sizeof(line), fd)) {
		char *cur = line;
		char device[512];
		char mp[512];
		char fs[32];

		cur += strspn(cur, " \t");
		if (!*cur || *cur == '\n')
			continue;

		if (nfs_parse_device_line(line, device, sizeof(device), mp, sizeof(mp), fs, sizeof(fs))) {
			if (in_nfs)
				nfs_emit_xprt(export, mount, xprt, nxprt);
			in_nfs = 0;
			in_perop = 0;
			nxprt = 0;
			memset(xprt, 0, sizeof(xprt));
			if (!nfs_is_nfs_fstype(fs))
				continue;
			strlcpy(export, device, sizeof(export));
			strlcpy(mount, mp, sizeof(mount));
			in_nfs = 1;
			continue;
		}

		if (!in_nfs)
			continue;

		if (!strncmp(cur, "per-op", 6)) {
			in_perop = 1;
			continue;
		}
		if (!strncmp(cur, "age:", 4)) {
			uint64_t age = 0;
			if (nfs_parse_uints(cur, &age, 1) == 1)
				metric_add_labels2("nfs_mount_age_seconds", &age, DATATYPE_UINT,
					ac->system_carg, "export", export, "mountpoint", mount);
			continue;
		}
		if (!strncmp(cur, "bytes:", 6)) {
			nfs_parse_bytes(cur, export, mount);
			continue;
		}
		if (!strncmp(cur, "events:", 7)) {
			nfs_parse_events(cur, export, mount);
			continue;
		}
		if (!strncmp(cur, "xprt:", 5)) {
			nfs_parse_xprt(cur, xprt, &nxprt);
			continue;
		}
		if (in_perop)
			nfs_parse_op(cur, export, mount);
	}

	if (in_nfs)
		nfs_emit_xprt(export, mount, xprt, nxprt);

	fclose(fd);
}

#endif
