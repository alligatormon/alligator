#ifdef __FreeBSD__
#include "main.h"
#include "common/logs.h"
#include "dstructures/ht.h"
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

static void split_hostport(const char *addrport, char *host, size_t host_sz, char *port, size_t port_sz)
{
	size_t i, last_dot = 0, dot_count = 0;

	host[0] = port[0] = 0;
	if (!addrport || !addrport[0])
		return;

	for (i = 0; addrport[i]; i++) {
		if (addrport[i] == '.')
			dot_count++, last_dot = i;
	}

	if (strchr(addrport, ':') && dot_count <= 1) {
		const char *colon = strrchr(addrport, ':');
		if (colon) {
			size_t hlen = (size_t)(colon - addrport);
			if (hlen >= host_sz)
				hlen = host_sz - 1;
			memcpy(host, addrport, hlen);
			host[hlen] = 0;
			strlcpy(port, colon + 1, port_sz);
			return;
		}
	}

	if (dot_count >= 1) {
		strlcpy(host, addrport, host_sz);
		if (last_dot < strlen(addrport))
			strlcpy(port, addrport + last_dot + 1, port_sz);
		host[last_dot] = 0;
		return;
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

static void process_lookup_add(alligator_ht *lookup, const char *proto, const char *local, const char *process)
{
	char host[128], port[16], key[256];
	uint32_t hash;
	process_lookup_entry *entry;

	split_hostport(local, host, sizeof(host), port, sizeof(port));
	snprintf(key, sizeof(key), "%s|%s|%s", proto, host, port);
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

	snprintf(cmd, sizeof(cmd), "sockstat -%s -P %s -c 2>/dev/null", family, proto);
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
	alligator_ht *count_port_usage, alligator_ht *recv_queue_stats, alligator_ht *process_lookup)
{
	char cmd[128];
	FILE *fp;
	char line[512];

	snprintf(cmd, sizeof(cmd), "netstat -anW -f %s -p %s 2>/dev/null",
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
		uint32_t key_hash;
		port_used *pu;
		char *process;

		if (strncmp(line, "Proto", 5) == 0 || line[0] == '\n')
			continue;

		if (sscanf(line, "%15s %lu %lu %127s %127s %31s", pr, &recv_q, &send_q, local, foreign, state) < 5)
			continue;

		state_id = tcp_state_id(state);
		if (pproto == IPPROTO_UDP)
			state_id = !strcmp(state, "*") || !strcmp(state, "") ? 7 : state_id;

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
				if (process)
					snprintf(queuekey, sizeof(queuekey), "%s:%s:%s:%s:%s:%s", src, srcp, dst, dstp, proto, process);
				else
					snprintf(queuekey, sizeof(queuekey), "%s:%s:%s:%s:%s", src, srcp, dst, dstp, proto);

				key_hash = tommy_strhash_u32(0, queuekey);
				socket_recv_queue_stat *sq = alligator_ht_search(recv_queue_stats,
					socket_recv_queue_stat_compare, queuekey, key_hash);
				if (!sq) {
					sq = calloc(1, sizeof(*sq));
					sq->key = strdup(queuekey);
					sq->src = strdup(src);
					sq->src_port = strdup(srcp);
					sq->dst = strdup(dst);
					sq->dst_port = strdup(dstp);
					sq->proto = strdup(proto);
					if (process) {
						sq->has_process = 1;
						sq->process = strdup(process);
					}
					sq->recv_queue_length = recv_q;
					sq->listen_backlog = send_q;
					alligator_ht_insert(recv_queue_stats, &(sq->node), sq, key_hash);
				} else {
					if (recv_q > sq->recv_queue_length)
						sq->recv_queue_length = recv_q;
					if (send_q > sq->listen_backlog)
						sq->listen_backlog = send_q;
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
			pu->local_addr = strdup(src);
			pu->key = strdup(srcaddrportkey);
			pu->count = 1;
			pu->state_proto = pproto;
			pu->state = state_id;
			if (process)
				pu->process = strdup(process);
			alligator_ht_insert(addr_port_usage, &(pu->node), pu, key_hash);
		} else {
			pu->count++;
		}

		key_hash = tommy_strhash_u32(0, src);
		count_port_used *count_addr = alligator_ht_search(count_port_usage,
			count_port_used_compare, src, key_hash);
		if (!count_addr) {
			count_addr = calloc(1, sizeof(*count_addr));
			count_addr->key = strdup(src);
			count_addr->count = alligator_ht_init(NULL);
			alligator_ht_insert(count_port_usage, &(count_addr->node), count_addr, key_hash);
		}

		key_hash = tommy_strhash_u32(0, srcp);
		if (!alligator_ht_search(count_addr->count, count_addr_port_used_compare, srcp, key_hash)) {
			count_addr_port_used *capu = calloc(1, sizeof(*capu));
			capu->key = strdup(srcp);
			alligator_ht_insert(count_addr->count, &(capu->node), capu, key_hash);
		}
	}
	pclose(fp);
}

static void check_sockets_proto(char *proto, uint8_t pproto, alligator_ht *process_lookup)
{
	alligator_ht *addr_port_usage;
	alligator_ht *count_port_usage;
	alligator_ht *recv_queue_stats;

	addr_port_usage = calloc(1, sizeof(*addr_port_usage));
	alligator_ht_init(addr_port_usage);
	count_port_usage = alligator_ht_init(NULL);
	recv_queue_stats = alligator_ht_init(NULL);

	parse_netstat(proto, pproto, addr_port_usage, count_port_usage, recv_queue_stats, process_lookup);

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

void check_sockets_freebsd(void)
{
	alligator_ht *process_lookup;

	process_lookup = alligator_ht_init(NULL);
	parse_sockstat(process_lookup, "4", "tcp");
	parse_sockstat(process_lookup, "6", "tcp");
	parse_sockstat(process_lookup, "4", "udp");
	parse_sockstat(process_lookup, "6", "udp");

	check_sockets_proto("tcp", IPPROTO_TCP, process_lookup);
	check_sockets_proto("tcp6", IPPROTO_TCP, process_lookup);
	check_sockets_proto("udp", IPPROTO_UDP, process_lookup);
	check_sockets_proto("udp6", IPPROTO_UDP, process_lookup);

	alligator_ht_foreach_arg(process_lookup, process_lookup_free_cb, NULL);
	alligator_ht_done(process_lookup);
	free(process_lookup);
}
#endif
