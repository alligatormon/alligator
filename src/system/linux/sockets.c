#include <netinet/in.h>
#include <netdb.h>
#include <net/if.h>
#include <net/route.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/stat.h>
#include <linux/if_bridge.h>
#include <linux/inet_diag.h>
#include <linux/sock_diag.h>
#include <linux/netlink.h>
#include <dirent.h>
#include <netpacket/packet.h>
#include "main.h"
#include "system/fdescriptors.h"
#define SOCKET_MAXN		192
#define TCP_CONN_MAXN		32
#define TCP_STATE_MAX sizeof(tcp_states) / sizeof(tcp_states[0])
extern aconf *ac;

typedef struct port_used {
	char *remote_addr;
	char *remote_port;
	char *local_addr;
	uint64_t count;
	int state_proto;
	int state;
	char *process;

	char *key;
	tommy_node node;
} port_used;

typedef struct count_port_used {
	alligator_ht *count;

	char *key;
	tommy_node node;
} count_port_used;

typedef struct count_addr_port_used {
	char *key;
	tommy_node node;
} count_addr_port_used;

int port_used_compare(const void* arg, const void* obj)
{
		char *s1 = (char*)arg;
		char *s2 = ((port_used*)obj)->key;
		return strcmp(s1, s2);
}

int count_port_used_compare(const void* arg, const void* obj)
{
		char *s1 = (char*)arg;
		char *s2 = ((count_port_used*)obj)->key;
		return strcmp(s1, s2);
}

int count_addr_port_used_compare(const void* arg, const void* obj)
{
		char *s1 = (char*)arg;
		char *s2 = ((count_addr_port_used*)obj)->key;
		return strcmp(s1, s2);
}

void port_used_free(void *funcarg, void* arg)
{
	port_used *pu = arg;
	if (!pu)
		return;

	int state_proto = pu->state_proto;
	int state = pu->state;

	char *proto = state_proto == IPPROTO_TCP ? "tcp" : "udp";

	if ((state_proto == IPPROTO_TCP && state == 10) || (state_proto == IPPROTO_UDP && state == 7))
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "LISTEN", "process", pu->process);
	else if (state == 6)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "TIME_WAIT", "process", pu->process);
	else if (state == 1)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "ESTABLISHED", "process", pu->process);
	else if (state == 2)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "SYN_SENT", "process", pu->process);
	else if (state == 4)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "SYN_RECV", "process", pu->process);
	else if (state == 4)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "FIN_WAIT1", "process", pu->process);
	else if (state == 5)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "FIN_WAIT2", "process", pu->process);
	else if (state == 7)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "CLOSE", "process", pu->process);
	else if (state == 8)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "CLOSE_WAIT", "process", pu->process);
	else if (state == 9)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "LAST_ACK", "process", pu->process);
	else if (state == 11)
		metric_add_labels4("socket_counters", &pu->count, DATATYPE_UINT, ac->system_carg, "addr", pu->local_addr, "proto", proto, "state", "CLOSING", "process", pu->process);

	if (pu->remote_port)
		free(pu->remote_port);
	if (pu->local_addr)
		free(pu->local_addr);
	if (pu->remote_addr)
		free(pu->remote_addr);

	free(pu->key);
	free(pu);
}

void count_port_free(void *funcarg, void* arg)
{
	count_addr_port_used *capu = arg;
	if (!capu)
		return;

	free(capu->key);
	free(capu);
}

void count_port(void *funcarg, void* arg)
{
	count_port_used *cput = arg;
	if (!cput)
		return;

	char *proto = funcarg;

	uint64_t count = alligator_ht_count(cput->count);

	metric_add_labels2("port_usage_count", &count, DATATYPE_UINT, ac->system_carg, "addr", cput->key, "proto", proto);

	alligator_ht_foreach_arg(cput->count, count_port_free, NULL);
	alligator_ht_done(cput->count);
	free(cput->count);

	free(cput->key);
	free(cput);
}

void check_sockets_by_netlink(char *proto, uint8_t family, uint8_t pproto)
{
	r_time ts_start = setrtime();
	if (ac->log_level > 2)
		printf("system scrape metrics: network: get_net_tcpudp '%s'\n", proto);

	int type, socknum = 0;
	int got_states = 0;
	struct sockaddr_nl nladdr;
	struct msghdr msg;

	alligator_ht *addr_port_usage = calloc(1, sizeof(*addr_port_usage));
	alligator_ht_init(addr_port_usage);

	alligator_ht *count_port_usage = alligator_ht_init(NULL);

	char srcp[6];
	char destp[6];

	struct sockaddr_in6 sin6;
	struct {
		struct nlmsghdr nlh;
		//struct inet_diag_req r;
		struct inet_diag_req_v2 r;
	} req;

	int print_states=0;
	struct iovec iov[3];
	unsigned state;//, tx_queue, rx_queue;
	char sockmask[SOCKET_MAXN];
	char srcaddrportkey[255];
	int typemask[5];
	char fbuf[getpagesize() * 8];
	uint64_t tcp_connections[TCP_CONN_MAXN];
	bzero (tcp_connections, sizeof(tcp_connections));

	struct {
		unsigned long long count;
		char * state;
	} tcp_states[] = {
		{ 0, "undef",		/* not a state */ },
		{ 0, "ESTABLISHED",	/* TCP_ESTABLISHED */ },
		{ 0, "SYN_SENT",	/* TCP_SYN_SENT	*/ },
		{ 0, "SYN_RECV",	/* TCP_SYN_RECV	*/ },
		{ 0, "FIN_WAIT1",	/* TCP_FIN_WAIT1 */ },
		{ 0, "FIN_WAIT2",	/* TCP_FIN_WAIT2 */ },
		{ 0, "TIME_WAIT",	/* TCP_TIME_WAIT */ },
		{ 0, "CLOSE",		/* TCP_CLOSE */ },
		{ 0, "CLOSE_WAIT",	/* TCP_CLOSE_WAIT */ },
		{ 0, "LAST_ACK",	/* TCP_LAST_ACK	*/ },
		{ 0, "LISTEN",		/* TCP_LISTEN */ },
		{ 0, "CLOSING",		/* TCP_CLOSING */ },
	};

	int nld = socket(AF_NETLINK, SOCK_RAW, NETLINK_INET_DIAG);
	if (nld != -1)
	{
		bzero(&nladdr, sizeof(nladdr));
		nladdr.nl_family = AF_NETLINK;

		req.nlh.nlmsg_len = sizeof(req);
		req.nlh.nlmsg_type = SOCK_DIAG_BY_FAMILY;
		req.nlh.nlmsg_flags = NLM_F_REQUEST|NLM_F_DUMP;
		req.nlh.nlmsg_pid = 0;
		req.nlh.nlmsg_seq = 1111;
		bzero(&req.r, sizeof(req.r));
		req.r.idiag_states = 0xFFF;
		req.r.sdiag_protocol = pproto;
		req.r.sdiag_family = family;
		req.r.idiag_ext = 0 | 1<<(2-1);

		iov[0] = (struct iovec) {
			.iov_base = &req,
			.iov_len = sizeof(req)
		};
		msg = (struct msghdr) {
			.msg_name = (void*)&nladdr,
			.msg_namelen = sizeof(nladdr),
			.msg_iov = iov,
			.msg_iovlen = 1
		};
		int rep;
		if ((rep = sendmsg(nld, &msg, 0)) < 0)
			fprintf(stderr, "sendmsg: %d\n", rep);
		iov[0] = (struct iovec) {
			.iov_base = fbuf,
			.iov_len = sizeof(fbuf)
		};
		while(rep > 0)
		{
			int status;
			struct nlmsghdr *h;

			msg = (struct msghdr) {
				(void*)&nladdr, sizeof(nladdr),
				iov,	1,
				NULL,   0,
				0
			};
			status = recvmsg(nld, &msg, 0);
			if (status < 0) {
				if (errno == EINTR)
					continue;
				fprintf(stderr, "OVERRUN\n");
				continue;
			}
			if (status == 0)
				break;
			h = (struct nlmsghdr*)fbuf;

			while (status > 0 && NLMSG_OK(h, (uint) status)) {
				struct inet_diag_msg *r = NLMSG_DATA(h);
				if (h->nlmsg_seq != 1111) {
					h = NLMSG_NEXT(h, status);
					continue;
				}
				if (h->nlmsg_type == NLMSG_DONE || h->nlmsg_type == NLMSG_ERROR) {
					status = -1;
					break;
				}
				state = r->idiag_state;
				switch (r->idiag_family) {
				case AF_INET:
					type = 0;
					break;
				case AF_INET6:
					//sin6.sin6_port = r->id.idiag_sport;
					memcpy(&sin6.sin6_addr, r->id.idiag_src, 16);
					//inp = &sin6;
					type = 2;
					break;
				default:
					type = 5;
				}
				char src[20];
				char dst[20];
				inet_ntop(AF_INET, r->id.idiag_src, src, sizeof(src));
				inet_ntop(AF_INET, r->id.idiag_src, dst, sizeof(dst));
				uint16_t sport = htons(r->id.idiag_sport);
				uint16_t dport = htons(r->id.idiag_dport);
				snprintf(srcp, 6, "%"PRIu16, sport);
				snprintf(destp, 6, "%"PRIu16, dport);
				//printf("state %d, family %d, src addr %s, src port %u, dst adr %s, dst port %u rqueue %u, wqueue %u\n", state, r->idiag_family, src, sport, dst, dport, r->idiag_rqueue, r->idiag_wqueue);

				uint32_t fdesc = r->idiag_inode;
				process_fdescriptors *fdescriptors = NULL;
				if (ac->fdesc)
					fdescriptors = alligator_ht_search(ac->fdesc, process_fdescriptors_compare, &fdesc, tommy_inthash_u32(fdesc));

				if (print_states &&
					state < TCP_STATE_MAX) {
					tcp_states[state].count ++;
					got_states = 1;
				}
				if (state == 10 &&
					 !sockmask[socknum]) {
					typemask[type] --;
					sockmask[socknum] = 1;
					fprintf(stderr,"got state 10 from netlink, qlen %d, qlimit %d", r->idiag_rqueue, r->idiag_wqueue);
				}

				//printf("state %d, family %d/%d/%d\n", state, pproto, IPPROTO_TCP, IPPROTO_UDP);
				if ((pproto == IPPROTO_TCP && state == 10) || (pproto == IPPROTO_UDP && state == 7))
				{
					uint64_t val = 1;

					uint64_t send_queue = r->idiag_rqueue;
					uint64_t recv_queue = r->idiag_wqueue;

					if (fdescriptors)
					{
						metric_add_labels7("socket_stat", &val, DATATYPE_UINT, ac->system_carg, "src", src, "src_port", srcp, "dst", dst, "dst_port", destp, "state", "LISTEN", "proto", proto, "process", fdescriptors->procname);
						metric_add_labels7("socket_stat_receive_queue_length", &send_queue, DATATYPE_UINT, ac->system_carg, "src", src, "src_port", srcp, "dst", dst, "dst_port", destp, "state", "LISTEN", "proto", proto, "process", fdescriptors->procname);
						metric_add_labels7("socket_stat_receive_queue_limit", &recv_queue, DATATYPE_UINT, ac->system_carg, "src", src, "src_port", srcp, "dst", dst, "dst_port", destp, "state", "LISTEN", "proto", proto, "process", fdescriptors->procname);
					}
					else
					{
						metric_add_labels6("socket_stat", &val, DATATYPE_UINT, ac->system_carg, "src", src, "src_port", srcp, "dst", dst, "dst_port", destp, "state", "LISTEN", "proto", proto);
						metric_add_labels6("socket_stat_receive_queue_length", &send_queue, DATATYPE_UINT, ac->system_carg, "src", src, "src_port", srcp, "dst", dst, "dst_port", destp, "state", "LISTEN", "proto", proto);
						metric_add_labels6("socket_stat_receive_queue_limit", &recv_queue, DATATYPE_UINT, ac->system_carg, "src", src, "src_port", srcp, "dst", dst, "dst_port", destp, "state", "LISTEN", "proto", proto);
					}
				}

				// calculate ports
				if (fdescriptors)
					snprintf(srcaddrportkey, 255, "%s:%d:%s", src, state, fdescriptors->procname);
				else
					snprintf(srcaddrportkey, 255, "%s:%d", src, state);

				uint32_t key_hash = tommy_strhash_u32(0, srcaddrportkey);
				port_used *pu = alligator_ht_search(addr_port_usage, port_used_compare, srcaddrportkey, key_hash);
				if (!pu)
				{
					pu = calloc(1, sizeof(*pu));
					pu->local_addr = strdup(src);
					pu->key = strdup(srcaddrportkey);
					pu->count = 1;
					pu->state_proto = pproto;
					pu->state = state;
					pu->process = (fdescriptors && fdescriptors->procname) ? fdescriptors->procname : "";

					alligator_ht_insert(addr_port_usage, &(pu->node), pu, key_hash);
				}
				else
				{
					++pu->count;
				}

				key_hash = tommy_strhash_u32(0, src);
				count_port_used* count_addr = alligator_ht_search(count_port_usage, count_port_used_compare, src, key_hash);
				if (!count_addr)
				{
					count_addr = calloc(1, sizeof(*count_addr));
					count_addr->key = strdup(src);
					count_addr->count = alligator_ht_init(NULL);

					alligator_ht_insert(count_port_usage, &(count_addr->node), count_addr, key_hash);
				}

				key_hash = tommy_strhash_u32(0, srcp);
				count_addr_port_used *capu = alligator_ht_search(count_addr->count, count_addr_port_used_compare, srcp, key_hash);
				if (!capu)
				{
					capu = calloc(1, sizeof(*count_addr));
					capu->key = strdup(srcp);

					alligator_ht_insert(count_addr->count, &(capu->node), capu, key_hash);
				}

				h = NLMSG_NEXT(h, status);
			}
			if (status < 0)
				break;
		}
		if (rep > 0) {
			typemask[0] = 0;
			typemask[2] = 0;
		}
		if (got_states)
			print_states = 0;
		close(nld);
	}

	alligator_ht_foreach_arg(count_port_usage, count_port, proto);
	alligator_ht_foreach_arg(addr_port_usage, port_used_free, NULL);
	alligator_ht_done(addr_port_usage);
	alligator_ht_done(count_port_usage);
	free(addr_port_usage);
	free(count_port_usage);

	r_time ts_end = setrtime();
	int64_t scrape_time = getrtime_ns(ts_start, ts_end);
	if (ac->log_level > 2)
		printf("system scrape metrics: network: get_net_tcpudp time execute '%"d64"'\n", scrape_time);
}
