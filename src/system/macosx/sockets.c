#ifdef __APPLE__
#include "main.h"
#include "common/logs.h"
#include "dstructures/ht.h"
#include <arpa/inet.h>
#include <libproc.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/param.h>
#include <sys/proc_info.h>
#include <sys/socket.h>

extern aconf *ac;

typedef struct port_used {
	char *key;
	char *local_addr;
	char *process;
	uint64_t count;
	int state_proto;
	int state;
	tommy_node node;
} port_used;

typedef struct count_port_used {
	char *key;
	alligator_ht *count;
	tommy_node node;
} count_port_used;

typedef struct count_addr_port_used {
	char *key;
	tommy_node node;
} count_addr_port_used;

typedef struct socket_recv_queue_stat {
	char *key;
	char *src;
	char *src_port;
	char *dst;
	char *dst_port;
	char *proto;
	char *process;
	int has_process;
	uint64_t recv_queue_length;
	uint64_t listen_backlog;
	tommy_node node;
} socket_recv_queue_stat;

static int port_used_compare(const void *arg, const void *obj)
{
	return strcmp((char *)arg, ((port_used *)obj)->key);
}

static int count_port_used_compare(const void *arg, const void *obj)
{
	return strcmp((char *)arg, ((count_port_used *)obj)->key);
}

static int count_addr_port_used_compare(const void *arg, const void *obj)
{
	return strcmp((char *)arg, ((count_addr_port_used *)obj)->key);
}

static int socket_recv_queue_stat_compare(const void *arg, const void *obj)
{
	return strcmp((char *)arg, ((socket_recv_queue_stat *)obj)->key);
}

typedef struct process_lookup_entry {
	char *key;
	char *process;
	tommy_node node;
} process_lookup_entry;

static int process_lookup_compare(const void *arg, const void *obj)
{
	return strcmp((char *)arg, ((process_lookup_entry *)obj)->key);
}

static int tcp_state_id(const char *state)
{
	if (!strcmp(state, "ESTABLISHED")) return 1;
	if (!strcmp(state, "SYN_SENT")) return 2;
	if (!strcmp(state, "SYN_RECV") || !strcmp(state, "SYN_RECEIVED")) return 3;
	if (!strcmp(state, "FIN_WAIT_1") || !strcmp(state, "FIN_WAIT1")) return 4;
	if (!strcmp(state, "FIN_WAIT_2") || !strcmp(state, "FIN_WAIT2")) return 5;
	if (!strcmp(state, "TIME_WAIT")) return 6;
	if (!strcmp(state, "CLOSED") || !strcmp(state, "CLOSE")) return 7;
	if (!strcmp(state, "CLOSE_WAIT")) return 8;
	if (!strcmp(state, "LAST_ACK")) return 9;
	if (!strcmp(state, "LISTEN")) return 10;
	if (!strcmp(state, "CLOSING")) return 11;
	return 0;
}

static int suffix_is_digits(const char *s)
{
	size_t i;

	if (!s || !s[0])
		return 0;
	for (i = 0; s[i]; i++) {
		if (s[i] < '0' || s[i] > '9')
			return 0;
	}
	return 1;
}

/*
 * Darwin netstat uses addr.port (last '.' separates the port) for both IPv4 and
 * IPv6, e.g. 127.0.0.1.80, ::1.8021, fe80::1:.5353, *.443. Prefer that form
 * before any host:port colon split so IPv6 is not corrupted.
 */
static void split_hostport(const char *addrport, char *host, size_t host_sz, char *port, size_t port_sz)
{
	size_t i, last_dot = 0, dot_count = 0;
	size_t hlen;

	host[0] = port[0] = 0;
	if (!addrport || !addrport[0])
		return;

	for (i = 0; addrport[i]; i++) {
		if (addrport[i] == '.')
			dot_count++, last_dot = i;
	}

	if (dot_count >= 1 && suffix_is_digits(addrport + last_dot + 1)) {
		hlen = last_dot;
		if (hlen >= host_sz)
			hlen = host_sz - 1;
		memcpy(host, addrport, hlen);
		host[hlen] = 0;
		/* IPv6 quirk: "fe80::1:.80" leaves a trailing ':' on the host. */
		while (hlen > 0 && host[hlen - 1] == ':') {
			/* Keep a lone "::" / trailing compression intact when host is only colons. */
			if (hlen >= 2 && host[hlen - 2] == ':')
				break;
			host[--hlen] = 0;
		}
		strlcpy(port, addrport + last_dot + 1, port_sz);
		return;
	}

	if (strchr(addrport, ':')) {
		const char *colon = strrchr(addrport, ':');

		if (colon && suffix_is_digits(colon + 1)) {
			hlen = (size_t)(colon - addrport);
			if (hlen >= host_sz)
				hlen = host_sz - 1;
			memcpy(host, addrport, hlen);
			host[hlen] = 0;
			strlcpy(port, colon + 1, port_sz);
			return;
		}
	}

	strlcpy(host, addrport, host_sz);
}

static void port_used_free_cb(void *funcarg, void *arg)
{
	port_used *pu = arg;
	char *proto;

	if (!pu)
		return;

	proto = pu->state_proto == IPPROTO_TCP ? "tcp" : "udp";

	if ((pu->state_proto == IPPROTO_TCP && pu->state == 10) || (pu->state_proto == IPPROTO_UDP && pu->state == 7))
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg,
			"addr", pu->local_addr, "proto", proto, "state", "LISTEN", "process", pu->process ? pu->process : "");
	else if (pu->state == 6)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg,
			"addr", pu->local_addr, "proto", proto, "state", "TIME_WAIT", "process", pu->process ? pu->process : "");
	else if (pu->state == 1)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg,
			"addr", pu->local_addr, "proto", proto, "state", "ESTABLISHED", "process", pu->process ? pu->process : "");
	else if (pu->state == 2)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg,
			"addr", pu->local_addr, "proto", proto, "state", "SYN_SENT", "process", pu->process ? pu->process : "");
	else if (pu->state == 3)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg,
			"addr", pu->local_addr, "proto", proto, "state", "SYN_RECV", "process", pu->process ? pu->process : "");
	else if (pu->state == 4)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg,
			"addr", pu->local_addr, "proto", proto, "state", "FIN_WAIT1", "process", pu->process ? pu->process : "");
	else if (pu->state == 5)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg,
			"addr", pu->local_addr, "proto", proto, "state", "FIN_WAIT2", "process", pu->process ? pu->process : "");
	else if (pu->state == 7)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg,
			"addr", pu->local_addr, "proto", proto, "state", "CLOSE", "process", pu->process ? pu->process : "");
	else if (pu->state == 8)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg,
			"addr", pu->local_addr, "proto", proto, "state", "CLOSE_WAIT", "process", pu->process ? pu->process : "");
	else if (pu->state == 9)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg,
			"addr", pu->local_addr, "proto", proto, "state", "LAST_ACK", "process", pu->process ? pu->process : "");
	else if (pu->state == 11)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg,
			"addr", pu->local_addr, "proto", proto, "state", "CLOSING", "process", pu->process ? pu->process : "");

	free(pu->key);
	free(pu->local_addr);
	if (pu->process)
		free(pu->process);
	free(pu);
}

static void count_port_free_cb(void *funcarg, void *arg)
{
	count_addr_port_used *capu = arg;
	free(capu->key);
	free(capu);
}

static void count_port_cb(void *funcarg, void *arg)
{
	count_port_used *cput = arg;
	uint64_t count;

	if (!cput)
		return;

	count = alligator_ht_count(cput->count);
	metric_add_labels2("port_usage_count", &count, DATATYPE_UINT, ac->system_carg,
		"addr", cput->key, "proto", (char *)funcarg);

	alligator_ht_foreach_arg(cput->count, count_port_free_cb, NULL);
	alligator_ht_done(cput->count);
	free(cput->count);
	free(cput->key);
	free(cput);
}

static void socket_recv_queue_stat_free_cb(void *funcarg, void *arg)
{
	socket_recv_queue_stat *sq = arg;

	if (!sq)
		return;

	if (sq->has_process) {
		metric_add_labels7("socket_stat_receive_queue_length", &sq->recv_queue_length, DATATYPE_UINT, ac->system_carg,
			"src", sq->src, "src_port", sq->src_port, "dst", sq->dst, "dst_port", sq->dst_port,
			"state", "LISTEN", "proto", sq->proto, "process", sq->process);
		metric_add_labels7("socket_stat_receive_queue_limit", &sq->listen_backlog, DATATYPE_UINT, ac->system_carg,
			"src", sq->src, "src_port", sq->src_port, "dst", sq->dst, "dst_port", sq->dst_port,
			"state", "LISTEN", "proto", sq->proto, "process", sq->process);
	} else {
		metric_add_labels6("socket_stat_receive_queue_length", &sq->recv_queue_length, DATATYPE_UINT, ac->system_carg,
			"src", sq->src, "src_port", sq->src_port, "dst", sq->dst, "dst_port", sq->dst_port,
			"state", "LISTEN", "proto", sq->proto);
		metric_add_labels6("socket_stat_receive_queue_limit", &sq->listen_backlog, DATATYPE_UINT, ac->system_carg,
			"src", sq->src, "src_port", sq->src_port, "dst", sq->dst, "dst_port", sq->dst_port,
			"state", "LISTEN", "proto", sq->proto);
	}

	free(sq->key);
	free(sq->src);
	free(sq->src_port);
	free(sq->dst);
	free(sq->dst_port);
	free(sq->proto);
	if (sq->has_process)
		free(sq->process);
	free(sq);
}

static void process_lookup_add_parts(alligator_ht *lookup, const char *proto,
	const char *host, const char *port, const char *process)
{
	char key[256];
	uint32_t hash;
	process_lookup_entry *entry;

	snprintf(key, sizeof(key), "%s|%s|%s", proto, host ? host : "", port ? port : "");
	hash = tommy_strhash_u32(0, key);
	if (alligator_ht_search(lookup, process_lookup_compare, key, hash))
		return;
	entry = calloc(1, sizeof(*entry));
	if (!entry)
		return;
	entry->key = strdup(key);
	entry->process = strdup(process ? process : "");
	if (!entry->key || !entry->process) {
		free(entry->key);
		free(entry->process);
		free(entry);
		return;
	}
	alligator_ht_insert(lookup, &(entry->node), entry, hash);
}

static void process_lookup_add(alligator_ht *lookup, const char *proto, const char *local, const char *process)
{
	char host[128], port[16];

	split_hostport(local, host, sizeof(host), port, sizeof(port));
	process_lookup_add_parts(lookup, proto, host, port, process);
}

/*
 * Darwin netstat reports Recv-Q/Send-Q as 0 for most LISTEN sockets.
 * libproc socket_info.soi_qlen / soi_qlimit are the accept-queue analogues
 * of Linux inet_diag idiag_rqueue / idiag_wqueue.
 */
typedef struct listen_queue_entry {
	char *key;
	uint64_t qlen;
	uint64_t qlimit;
	tommy_node node;
} listen_queue_entry;

static int listen_queue_compare(const void *arg, const void *obj)
{
	return strcmp((char *)arg, ((listen_queue_entry *)obj)->key);
}

static void listen_queue_free_cb(void *funcarg, void *arg)
{
	listen_queue_entry *e = arg;

	(void)funcarg;
	if (!e)
		return;
	free(e->key);
	free(e);
}

static void listen_queue_normalize_addr(const char *addr, int is_v6, char *out, size_t outsz)
{
	if (!addr || !addr[0] || !strcmp(addr, "*") || !strcmp(addr, "0.0.0.0") || !strcmp(addr, "::")
	    || !strcmp(addr, "::0")) {
		strlcpy(out, is_v6 ? "::" : "0.0.0.0", outsz);
		return;
	}
	strlcpy(out, addr, outsz);
}

static void listen_queue_make_key(char *key, size_t keysz, const char *proto,
	const char *addr, const char *port)
{
	int is_v6 = (proto && strstr(proto, "6") != NULL);
	char norm[128];

	listen_queue_normalize_addr(addr, is_v6, norm, sizeof(norm));
	snprintf(key, keysz, "%s|%s|%s", proto ? proto : "tcp", norm, port ? port : "0");
}

static void listen_queue_upsert_key(alligator_ht *cache, const char *key,
	uint64_t qlen, uint64_t qlimit)
{
	uint32_t hash;
	listen_queue_entry *e;

	hash = tommy_strhash_u32(0, key);
	e = alligator_ht_search(cache, listen_queue_compare, key, hash);
	if (!e) {
		e = calloc(1, sizeof(*e));
		if (!e)
			return;
		e->key = strdup(key);
		if (!e->key) {
			free(e);
			return;
		}
		e->qlen = qlen;
		e->qlimit = qlimit;
		alligator_ht_insert(cache, &(e->node), e, hash);
		return;
	}
	if (qlen > e->qlen)
		e->qlen = qlen;
	if (qlimit > e->qlimit)
		e->qlimit = qlimit;
}

static void listen_queue_upsert(alligator_ht *cache, const char *proto,
	const char *addr, const char *port, uint64_t qlen, uint64_t qlimit)
{
	char key[256];
	char portkey[128];

	listen_queue_make_key(key, sizeof(key), proto, addr, port);
	listen_queue_upsert_key(cache, key, qlen, qlimit);

	/* Port-only key for link-local / address-format mismatches vs netstat. */
	snprintf(portkey, sizeof(portkey), "%s||%s", proto ? proto : "tcp", port ? port : "0");
	listen_queue_upsert_key(cache, portkey, qlen, qlimit);
}

static int listen_queue_lookup(alligator_ht *cache, const char *proto,
	const char *addr, const char *port, uint64_t *qlen, uint64_t *qlimit)
{
	char key[256];
	uint32_t hash;
	listen_queue_entry *e;

	if (!cache)
		return 0;

	listen_queue_make_key(key, sizeof(key), proto, addr, port);
	hash = tommy_strhash_u32(0, key);
	e = alligator_ht_search(cache, listen_queue_compare, key, hash);
	if (!e && addr && strcmp(addr, "*") != 0) {
		/* Fallback: wildcard bind on the same port/family. */
		listen_queue_make_key(key, sizeof(key), proto, "*", port);
		hash = tommy_strhash_u32(0, key);
		e = alligator_ht_search(cache, listen_queue_compare, key, hash);
	}
	if (!e) {
		/* Fallback: any listener on this proto+port (link-local etc.). */
		snprintf(key, sizeof(key), "%s||%s", proto ? proto : "tcp", port ? port : "0");
		hash = tommy_strhash_u32(0, key);
		e = alligator_ht_search(cache, listen_queue_compare, key, hash);
	}
	if (!e)
		return 0;
	if (qlen)
		*qlen = e->qlen;
	if (qlimit)
		*qlimit = e->qlimit;
	return 1;
}

static void format_in_addr(const struct in_sockinfo *ini, int family, char *addr, size_t addrsz, char *port, size_t portsz)
{
	addr[0] = port[0] = 0;
	if (family == AF_INET) {
		inet_ntop(AF_INET, &ini->insi_laddr.ina_46.i46a_addr4, addr, addrsz);
		snprintf(port, portsz, "%u", ntohs(ini->insi_lport));
	} else if (family == AF_INET6) {
		inet_ntop(AF_INET6, &ini->insi_laddr.ina_6, addr, addrsz);
		snprintf(port, portsz, "%u", ntohs(ini->insi_lport));
	}
}

static void build_listen_queue_cache(alligator_ht *cache, alligator_ht *process_lookup)
{
	int bufsz;
	pid_t *pids;
	int n;
	int i;

	bufsz = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
	if (bufsz <= 0)
		return;
	/* Over-allocate: process list can grow between size probe and fill. */
	if (bufsz < (int)sizeof(pid_t) * 64)
		bufsz = (int)sizeof(pid_t) * 64;
	bufsz *= 2;

	pids = calloc(1, bufsz);
	if (!pids)
		return;

	n = proc_listpids(PROC_ALL_PIDS, 0, pids, bufsz) / (int)sizeof(pid_t);
	for (i = 0; i < n; i++) {
		pid_t pid = pids[i];
		int fdbytes;
		struct proc_fdinfo *fds;
		int nfd;
		int j;
		char procname[2 * MAXCOMLEN] = "";

		if (pid <= 0)
			continue;

		fdbytes = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, NULL, 0);
		if (fdbytes <= 0)
			continue;
		fdbytes *= 2;

		fds = malloc(fdbytes);
		if (!fds)
			continue;

		nfd = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds, fdbytes) / (int)sizeof(*fds);
		if (nfd <= 0) {
			free(fds);
			continue;
		}

		proc_name(pid, procname, sizeof(procname));

		for (j = 0; j < nfd; j++) {
			struct socket_fdinfo si;
			const struct in_sockinfo *ini;
			char addr[INET6_ADDRSTRLEN];
			char port[16];
			const char *proto;
			int family;

			if (fds[j].proc_fdtype != PROX_FDTYPE_SOCKET)
				continue;
			if (proc_pidfdinfo(pid, fds[j].proc_fd, PROC_PIDFDSOCKETINFO, &si, sizeof(si)) <= 0)
				continue;
			if (si.psi.soi_kind != SOCKINFO_TCP)
				continue;
			if (si.psi.soi_proto.pri_tcp.tcpsi_state != TSI_S_LISTEN)
				continue;

			family = si.psi.soi_family;
			ini = &si.psi.soi_proto.pri_tcp.tcpsi_ini;
			format_in_addr(ini, family, addr, sizeof(addr), port, sizeof(port));
			if (!port[0])
				continue;

			proto = (family == AF_INET6) ? "tcp6" : "tcp";
			listen_queue_upsert(cache, proto, addr, port,
				(uint64_t)(uint16_t)si.psi.soi_qlen,
				(uint64_t)(uint16_t)si.psi.soi_qlimit);
			/* Dual-stack :: (IPV6_V6ONLY off) also appears as tcp wildcard in netstat. */
			if (family == AF_INET6 && (!strcmp(addr, "::") || !strcmp(addr, "::0"))) {
				listen_queue_upsert(cache, "tcp", "0.0.0.0", port,
					(uint64_t)(uint16_t)si.psi.soi_qlen,
					(uint64_t)(uint16_t)si.psi.soi_qlimit);
			}

			if (process_lookup && procname[0]) {
				process_lookup_add_parts(process_lookup, "tcp", addr, port, procname);
				if (!strcmp(addr, "0.0.0.0") || !strcmp(addr, "::") || !strcmp(addr, "::0"))
					process_lookup_add_parts(process_lookup, "tcp", "*", port, procname);
			}
		}
		free(fds);
	}
	free(pids);
}

static char *process_lookup_get(alligator_ht *lookup, const char *proto, const char *local)
{
	char host[128], port[16], key[256];
	uint32_t hash;
	process_lookup_entry *entry;

	split_hostport(local, host, sizeof(host), port, sizeof(port));
	snprintf(key, sizeof(key), "%s|%s|%s", proto, host, port);
	hash = tommy_strhash_u32(0, key);
	entry = alligator_ht_search(lookup, process_lookup_compare, key, hash);
	return entry ? entry->process : NULL;
}

static void process_lookup_free_cb(void *funcarg, void *arg)
{
	process_lookup_entry *entry = arg;
	if (!entry)
		return;
	free(entry->key);
	free(entry->process);
	free(entry);
}

static void parse_sockstat(alligator_ht *lookup, const char *family, const char *proto)
{
	char cmd[128];
	FILE *fp;
	char line[512];

	snprintf(cmd, sizeof(cmd), "sockstat -%s -p %s -c 2>/dev/null", family, proto);
	fp = popen(cmd, "r");
	if (!fp)
		return;

	while (fgets(line, sizeof(line), fp)) {
		char user[64], command[64], pid[16], fd[16], pr[16], local[128], foreign[128];

		if (sscanf(line, "%63s %63s %15s %15s %15s %127s %127s",
			user, command, pid, fd, pr, local, foreign) < 6)
			continue;
		process_lookup_add(lookup, proto, local, command);
	}
	pclose(fp);
}

static void parse_netstat(char *proto, uint8_t pproto, alligator_ht *addr_port_usage,
	alligator_ht *count_port_usage, alligator_ht *recv_queue_stats, alligator_ht *process_lookup,
	alligator_ht *listen_queues)
{
	char cmd[128];
	FILE *fp;
	char line[512];

	snprintf(cmd, sizeof(cmd), "netstat -an -f %s -p %s 2>/dev/null",
		!strcmp(proto, "tcp6") || !strcmp(proto, "udp6") ? "inet6" : "inet", proto);
	fp = popen(cmd, "r");
	if (!fp)
		return;

	while (fgets(line, sizeof(line), fp)) {
		char pr[16], local[128], foreign[128], state[32];
		unsigned long recv_q, send_q;
		char src[128], dst[128], srcp[16], dstp[16];
		char srcaddrportkey[256], queuekey[512];
		int state_id;
		int ntok;
		uint32_t key_hash;
		port_used *pu;
		char *process;

		if (strncmp(line, "Proto", 5) == 0 || line[0] == '\n')
			continue;

		/* UDP netstat lines often omit the state column; keep state defined. */
		state[0] = '\0';
		ntok = sscanf(line, "%15s %lu %lu %127s %127s %31s", pr, &recv_q, &send_q, local, foreign, state);
		if (ntok < 5)
			continue;

		state_id = tcp_state_id(state);
		if (pproto == IPPROTO_UDP)
			state_id = (ntok < 6 || !strcmp(state, "*") || !state[0]) ? 7 : state_id;

		split_hostport(local, src, sizeof(src), srcp, sizeof(srcp));
		split_hostport(foreign, dst, sizeof(dst), dstp, sizeof(dstp));
		process = process_lookup_get(process_lookup, pproto == IPPROTO_TCP ? "tcp" : "udp", local);

		if ((pproto == IPPROTO_TCP && state_id == 10) || (pproto == IPPROTO_UDP && state_id == 7)) {
			uint64_t val = 1;
			if (process)
				metric_add_labels7("socket_stat", &val, DATATYPE_UINT, ac->system_carg,
					"src", src, "src_port", srcp, "dst", dst, "dst_port", dstp, "state", "LISTEN", "proto", proto, "process", process);
			else
				metric_add_labels6("socket_stat", &val, DATATYPE_UINT, ac->system_carg,
					"src", src, "src_port", srcp, "dst", dst, "dst_port", dstp, "state", "LISTEN", "proto", proto);

			if (pproto == IPPROTO_TCP && state_id == 10) {
				uint64_t qlen = recv_q;
				uint64_t qlimit = send_q;

				/* Prefer libproc accept-queue stats over netstat Recv-Q/Send-Q. */
				listen_queue_lookup(listen_queues, proto, src, srcp, &qlen, &qlimit);

				if (process)
					snprintf(queuekey, sizeof(queuekey), "%s:%s:%s:%s:%s:%s", src, srcp, dst, dstp, proto, process);
				else
					snprintf(queuekey, sizeof(queuekey), "%s:%s:%s:%s:%s", src, srcp, dst, dstp, proto);

				key_hash = tommy_strhash_u32(0, queuekey);
				socket_recv_queue_stat *sq = alligator_ht_search(recv_queue_stats,
					socket_recv_queue_stat_compare, queuekey, key_hash);
				if (!sq) {
					sq = calloc(1, sizeof(*sq));
					if (!sq)
						continue;
					sq->key = strdup(queuekey);
					sq->src = strdup(src);
					sq->src_port = strdup(srcp);
					sq->dst = strdup(dst);
					sq->dst_port = strdup(dstp);
					sq->proto = strdup(proto);
					if (!sq->key || !sq->src || !sq->src_port || !sq->dst || !sq->dst_port || !sq->proto) {
						free(sq->key);
						free(sq->src);
						free(sq->src_port);
						free(sq->dst);
						free(sq->dst_port);
						free(sq->proto);
						free(sq);
						continue;
					}
					if (process) {
						sq->process = strdup(process);
						if (!sq->process) {
							free(sq->key);
							free(sq->src);
							free(sq->src_port);
							free(sq->dst);
							free(sq->dst_port);
							free(sq->proto);
							free(sq);
							continue;
						}
						sq->has_process = 1;
					}
					sq->recv_queue_length = qlen;
					sq->listen_backlog = qlimit;
					alligator_ht_insert(recv_queue_stats, &(sq->node), sq, key_hash);
				} else {
					if (qlen > sq->recv_queue_length)
						sq->recv_queue_length = qlen;
					if (qlimit > sq->listen_backlog)
						sq->listen_backlog = qlimit;
				}
			}
		}

		if (process)
			snprintf(srcaddrportkey, sizeof(srcaddrportkey), "%s:%d:%s", src, state_id, process);
		else
			snprintf(srcaddrportkey, sizeof(srcaddrportkey), "%s:%d", src, state_id);

		key_hash = tommy_strhash_u32(0, srcaddrportkey);
		pu = alligator_ht_search(addr_port_usage, port_used_compare, srcaddrportkey, key_hash);
		if (!pu) {
			pu = calloc(1, sizeof(*pu));
			if (!pu)
				continue;
			pu->local_addr = strdup(src);
			pu->key = strdup(srcaddrportkey);
			if (!pu->local_addr || !pu->key) {
				free(pu->local_addr);
				free(pu->key);
				free(pu);
				continue;
			}
			pu->count = 1;
			pu->state_proto = pproto;
			pu->state = state_id;
			if (process) {
				pu->process = strdup(process);
				if (!pu->process) {
					free(pu->local_addr);
					free(pu->key);
					free(pu);
					continue;
				}
			}
			alligator_ht_insert(addr_port_usage, &(pu->node), pu, key_hash);
		} else {
			pu->count++;
		}

		key_hash = tommy_strhash_u32(0, src);
		count_port_used *count_addr = alligator_ht_search(count_port_usage,
			count_port_used_compare, src, key_hash);
		if (!count_addr) {
			count_addr = calloc(1, sizeof(*count_addr));
			if (!count_addr)
				continue;
			count_addr->key = strdup(src);
			count_addr->count = alligator_ht_init(NULL);
			if (!count_addr->key || !count_addr->count) {
				free(count_addr->key);
				if (count_addr->count) {
					alligator_ht_done(count_addr->count);
					free(count_addr->count);
				}
				free(count_addr);
				continue;
			}
			alligator_ht_insert(count_port_usage, &(count_addr->node), count_addr, key_hash);
		}

		key_hash = tommy_strhash_u32(0, srcp);
		if (!alligator_ht_search(count_addr->count, count_addr_port_used_compare, srcp, key_hash)) {
			count_addr_port_used *capu = calloc(1, sizeof(*capu));
			if (!capu)
				continue;
			capu->key = strdup(srcp);
			if (!capu->key) {
				free(capu);
				continue;
			}
			alligator_ht_insert(count_addr->count, &(capu->node), capu, key_hash);
		}
	}
	pclose(fp);
}

static void check_sockets_proto(char *proto, uint8_t pproto, alligator_ht *process_lookup,
	alligator_ht *listen_queues)
{
	alligator_ht *addr_port_usage;
	alligator_ht *count_port_usage;
	alligator_ht *recv_queue_stats;

	addr_port_usage = calloc(1, sizeof(*addr_port_usage));
	alligator_ht_init(addr_port_usage);
	count_port_usage = alligator_ht_init(NULL);
	recv_queue_stats = alligator_ht_init(NULL);

	parse_netstat(proto, pproto, addr_port_usage, count_port_usage, recv_queue_stats,
		process_lookup, listen_queues);

	alligator_ht_foreach_arg(count_port_usage, count_port_cb, proto);
	alligator_ht_foreach_arg(recv_queue_stats, socket_recv_queue_stat_free_cb, NULL);
	alligator_ht_foreach_arg(addr_port_usage, port_used_free_cb, NULL);

	alligator_ht_done(addr_port_usage);
	alligator_ht_done(count_port_usage);
	alligator_ht_done(recv_queue_stats);
	free(addr_port_usage);
	free(count_port_usage);
	free(recv_queue_stats);
}

void check_sockets_macos(void)
{
	alligator_ht *process_lookup;
	alligator_ht *listen_queues;

	process_lookup = alligator_ht_init(NULL);
	listen_queues = alligator_ht_init(NULL);

	parse_sockstat(process_lookup, "4", "tcp");
	parse_sockstat(process_lookup, "6", "tcp");
	parse_sockstat(process_lookup, "4", "udp");
	parse_sockstat(process_lookup, "6", "udp");
	build_listen_queue_cache(listen_queues, process_lookup);

	check_sockets_proto("tcp", IPPROTO_TCP, process_lookup, listen_queues);
	check_sockets_proto("tcp6", IPPROTO_TCP, process_lookup, listen_queues);
	check_sockets_proto("udp", IPPROTO_UDP, process_lookup, listen_queues);
	check_sockets_proto("udp6", IPPROTO_UDP, process_lookup, listen_queues);

	alligator_ht_foreach_arg(listen_queues, listen_queue_free_cb, NULL);
	alligator_ht_done(listen_queues);
	free(listen_queues);

	alligator_ht_foreach_arg(process_lookup, process_lookup_free_cb, NULL);
	alligator_ht_done(process_lookup);
	free(process_lookup);
}
#endif
