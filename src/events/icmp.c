#include "events/context_arg.h"
#ifdef __linux__
#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include </usr/include/netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <inttypes.h>
#include <netinet/in.h>
#include "dstructures/tommy.h"
#include "common/logs.h"
#include "common/rtime.h"
#include "probe/probe.h"
#include "parsers/multiparser.h"
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "main.h"
#define PACKETSIZE 64
#define ICMP_PACKET_MAX 1472

extern aconf *ac;

typedef struct {
	struct icmphdr hdr;
	uint32_t ts;
	uint8_t payload[ICMP_PACKET_MAX - sizeof(struct icmphdr) - sizeof(uint32_t)];
} icmp_packet_t;

typedef union packet_u {
	uint8_t raw[ICMP_PACKET_MAX + 64];
	icmp_packet_t icmp_req;
	struct {
		struct ip iphdr;
		icmp_packet_t pckt;
	} icmp_res;
} packet_t;

static size_t icmp_send_len(context_arg *carg)
{
	size_t hdr = sizeof(struct icmphdr) + sizeof(uint32_t);
	size_t payload = PACKETSIZE - hdr;
	size_t total;

	if (carg && carg->icmp_payload_size)
		payload = carg->icmp_payload_size;
	total = hdr + payload;
	if (total > ICMP_PACKET_MAX)
		total = ICMP_PACKET_MAX;
	if (total < hdr)
		total = hdr;
	return total;
}

static uint64_t icmp_splay_ms(context_arg *carg)
{
	uint64_t interval;
	uint32_t h;

	if (!carg || !carg->icmp_continuous || !carg->host[0])
		return 0;
	interval = carg->icmp_interval_ms ? carg->icmp_interval_ms : carg->packets_send_period;
	if (interval < 2)
		return 0;
	h = tommy_strhash_u32(0, carg->host);
	return (uint64_t)(h % interval);
}
void on_socket_ready (uv_poll_t *req, int status, int events);
void socket_write_mode (context_arg *carg, int isOn);
void on_towrite (uv_timer_t *handle);

/* standard 1s complement checksum */
static uint16_t
checksum (void *b, int len)
{	
	unsigned short *buf = (unsigned short *) b;
	unsigned int sum=0;
	unsigned short result;

	for ( sum = 0; len > 1; len -= 2 )
		sum += *buf++;
	if ( len == 1 )
		sum += *(unsigned char*)buf;
	sum = (sum >> 16) + (sum & 0xFFFF);
	sum += (sum >> 16);
	result = ~sum;
	return result;
}

static uint32_t
get_monotonic_time () {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static uint32_t
get_monotonic_time_diff (uint32_t start, uint32_t end) {
	return end - start;
}

static void
dump_packet (context_arg *carg, icmp_packet_t *icmp) {
	uint8_t *raw = (uint8_t *) icmp;
	uint32_t te = get_monotonic_time();

	carglog(carg, L_DEBUG, "type: %d code: %d checksum: %d/%d id: %d seq: %d time diff: %d ms\n",
			icmp->hdr.type, icmp->hdr.code,
			icmp->hdr.checksum, checksum(icmp, sizeof(*icmp)),
			icmp->hdr.un.echo.id, icmp->hdr.un.echo.sequence,
			get_monotonic_time_diff(icmp->ts, te)
			);
	unsigned int i;
	for (i = 0; i < sizeof(*icmp); i++) {
		if (i % 16 == 0) carglog(carg, L_DEBUG, "\n");
		carglog(carg, L_DEBUG, "%02X ", raw[i]);
	}
	carglog(carg, L_DEBUG,"\n------------------------------------\n");
}

void icmp_stop_run (context_arg *carg) {
	uv_timer_stop(&carg->t_timeout);
	uv_timer_stop(&carg->t_towrite);
	uv_timer_stop(&carg->t_seq_timer);
	uv_poll_stop(&carg->poll_socket);
	close(carg->fd);

	carg->lock = 0;
	(carg->read_counter)++;
	metric_add_labels("icmp_entrypoint_read", &carg->read_counter, DATATYPE_UINT, carg, "entrypoint", carg->key);

	if (carg->period)
		uv_timer_set_repeat(carg->period_timer, carg->period);
}

void icmp_metrics(context_arg *carg)
{
	double success_percent = 0.0;
	if (carg->sequence_success)
		success_percent = carg->sequence_success * 1.0 / carg->pingloop;

	double error_percent = 0.0;
	if (carg->sequence_error)
		error_percent = carg->sequence_error * 1.0 / carg->pingloop;

	uint64_t total_time = carg->timeout_time * 1000;
	uint64_t read_time = carg->sequence_time * 1000;
	uint64_t resolve_time = getrtime_mcs(carg->resolve_time, carg->resolve_time_finish, 0);
	if (success_percent > 0)
	{
		read_time = getrtime_mcs(carg->read_time, carg->read_time_finish, 0);
		total_time = getrtime_mcs(carg->total_time, carg->total_time_finish, 0);
	}

	carglog(carg, L_INFO, "icmp: summary host=%s success=%"PRIu64" error=%"PRIu64" success_pct=%lf error_pct=%lf resolve_ms=%"PRIu64" read_ms=%"PRIu64" total_ms=%"PRIu64" tries=%"PRIu64"\n", carg->host, carg->sequence_success, carg->sequence_error, success_percent, error_percent, resolve_time, read_time, total_time, carg->pingloop);

	namespace_metric_family_set(NULL, carg, "aggregator_read_time", METRIC_TYPE_GAUGE, "Last ICMP burst read time in microseconds.");
	namespace_metric_family_set(NULL, carg, "aggregator_resolve_time", METRIC_TYPE_GAUGE, "DNS resolve duration in microseconds.");
	namespace_metric_family_set(NULL, carg, "aggregator_packet_received", METRIC_TYPE_GAUGE, "ICMP echo replies in the last burst.");
	namespace_metric_family_set(NULL, carg, "aggregator_packet_received_ratio", METRIC_TYPE_GAUGE, "ICMP echo reply ratio in the last burst (0–1).");
	namespace_metric_family_set(NULL, carg, "aggregator_packet_loss", METRIC_TYPE_GAUGE, "ICMP echoes lost in the last burst.");
	namespace_metric_family_set(NULL, carg, "aggregator_packet_loss_ratio", METRIC_TYPE_GAUGE, "ICMP echo loss ratio in the last burst (0–1).");
	namespace_metric_family_set(NULL, carg, "aggregator_packet_received_total", METRIC_TYPE_COUNTER, "ICMP echo replies received across bursts.");
	namespace_metric_family_set(NULL, carg, "aggregator_packet_loss_total", METRIC_TYPE_COUNTER, "ICMP echoes lost across bursts.");
	namespace_metric_family_set(NULL, carg, "aggregator_packet_sent_total", METRIC_TYPE_COUNTER, "ICMP echoes attempted across bursts.");
	namespace_metric_family_set(NULL, carg, "alligator_icmp_rtt_seconds", METRIC_TYPE_GAUGE, "Last ICMP echo round-trip time in seconds.");
	namespace_metric_family_set(NULL, carg, "alligator_icmp_reply_hop_limit", METRIC_TYPE_GAUGE, "TTL/hop-limit of the last ICMP echo reply.");
	namespace_metric_family_set(NULL, carg, "probe_success", METRIC_TYPE_GAUGE, "Whether the last ICMP burst met the success percent.");

	metric_add_labels2("aggregator_read_time", &read_time, DATATYPE_UINT, carg, "type", "icmp", "host", carg->host);
	metric_add_labels2("aggregator_resolve_time", &resolve_time, DATATYPE_UINT, carg, "type", "icmp", "host", carg->host);
	metric_add_labels2("aggregator_packet_received", &carg->sequence_success, DATATYPE_UINT, carg, "type", "icmp", "host", carg->host);
	metric_add_labels2("aggregator_packet_received_ratio", &success_percent, DATATYPE_DOUBLE, carg, "type", "icmp", "host", carg->host);
	metric_add_labels2("aggregator_packet_loss", &carg->sequence_error, DATATYPE_UINT, carg, "type", "icmp", "host", carg->host);
	metric_add_labels2("aggregator_packet_loss_ratio", &error_percent, DATATYPE_DOUBLE, carg, "type", "icmp", "host", carg->host);

	if (carg->icmp_last_rtt_ms) {
		double rtt_s = carg->icmp_last_rtt_ms / 1000.0;
		metric_add_labels2("alligator_icmp_rtt_seconds", &rtt_s, DATATYPE_DOUBLE, carg, "type", "icmp", "host", carg->host);
	}
	if (carg->icmp_last_ttl) {
		uint64_t hop = carg->icmp_last_ttl;
		metric_add_labels2("alligator_icmp_reply_hop_limit", &hop, DATATYPE_UINT, carg, "type", "icmp", "host", carg->host);
	}

	uint64_t sent = carg->sequence_success + carg->sequence_error;
	metric_update_labels2("aggregator_packet_received_total", &carg->sequence_success, DATATYPE_UINT, carg, "type", "icmp", "host", carg->host);
	metric_update_labels2("aggregator_packet_loss_total", &carg->sequence_error, DATATYPE_UINT, carg, "type", "icmp", "host", carg->host);
	metric_update_labels2("aggregator_packet_sent_total", &sent, DATATYPE_UINT, carg, "type", "icmp", "host", carg->host);

	if (carg->parser_handler == blackbox_null)
	{
		uint64_t val = 0;
		if (success_percent >= carg->pingpercent_success)
			val = 1;
		if (carg->icmp_duplicates) {
			namespace_metric_family_set(NULL, carg, "aggregator_packet_duplicate", METRIC_TYPE_GAUGE, "Duplicate ICMP echo replies in the last burst.");
			metric_add_labels2("aggregator_packet_duplicate", &carg->icmp_duplicates, DATATYPE_UINT, carg, "type", "icmp", "host", carg->host);
		}
		probe_metric_success(carg, val);
	}
}

int icmp_stop (context_arg *carg) {
	if (carg->lock) {
		icmp_stop_run(carg);
	}

	return 1;
}

void icmp_emit_one (context_arg* carg, icmp_packet_t* icmp, int hop_limit)
{
	uint32_t ts = get_monotonic_time();
	carg->read_time_finish = setrtime();
	uint32_t icmp_code_id = icmp->hdr.code;
	uint32_t icmp_type_id = icmp->hdr.type;
	uint32_t time_diff = get_monotonic_time_diff(icmp->ts, ts);
	double rtt_s = time_diff / 1000.0;

	++carg->sequence_done;
	++carg->sequence_success;
	uv_timer_stop(&carg->t_seq_timer);
	carg->icmp_last_rtt_ms = time_diff;
	if (hop_limit >= 0)
		carg->icmp_last_ttl = (uint8_t)hop_limit;
	carglog(carg, L_DEBUG, "icmp: reply host=%s code=%"PRIu32" type=%"PRIu32" rtt_ms=%"PRIu32" hop=%d done=%"PRIu64"/%"PRIu64"\n", carg->host, icmp_type_id, icmp_code_id, time_diff, hop_limit, carg->sequence_done, carg->pingloop);

	if (carg->icmp_continuous) {
		/* alligator_icmp_* only. Historical smokeping_prober PromQL used smokeping_response_duration_seconds; that alias is not emitted. */
		probe_histogram_observe(carg, "alligator_icmp_response_duration_seconds", NULL, rtt_s);
		icmp_metrics(carg);
		socket_write_mode(carg, 1);
		uv_timer_start(&carg->t_towrite, on_towrite, carg->icmp_interval_ms ? carg->icmp_interval_ms : carg->packets_send_period, 0);
		return;
	}

	if (carg->sequence_done >= carg->pingloop) {
		carglog(carg, L_DEBUG, "icmp: host=%s success=%d error=%d\n", carg->host, carg->sequence_success, carg->sequence_error);
		carg->total_time_finish = setrtime();
		icmp_metrics(carg);
		icmp_stop_run (carg);
	}
	else
	{
		socket_write_mode(carg, 1);
		uv_timer_start(&carg->t_towrite, on_towrite, 0, carg->packets_send_period);
	}
}

void socket_write_mode (context_arg *carg, int isOn) {
	if (isOn) {
		uv_poll_start(&carg->poll_socket, UV_READABLE | UV_WRITABLE, on_socket_ready);
	} else {
		carg->read_time = setrtime();
		uv_poll_start(&carg->poll_socket, UV_READABLE, on_socket_ready);
	}
}

void on_timeout (uv_timer_t *handle) {
	context_arg *carg = handle->data;
	++carg->sequence_error;
	carglog(carg, L_WARN, "icmp: timeout host=%s\n", carg->host);
	icmp_metrics(carg);
	icmp_stop_run(carg);
}

void on_towrite (uv_timer_t *handle) {
	context_arg *carg = handle->data;
	socket_write_mode(carg, 1);
}

void on_seq_timer (uv_timer_t *handle) {
	context_arg *carg = handle->data;
	++carg->sequence_done;
	++carg->sequence_error;
	carglog(carg, L_DEBUG, "icmp: sequence host=%s done=%"PRIu64"\n", carg->host, carg->sequence_done);
	//++carg->sequence_done;
	socket_write_mode(carg, 1);
	uv_timer_start(&carg->t_towrite, on_towrite, 0, carg->packets_send_period);
}

int ping_struct_compare(const void* arg, const void* obj)
{
	uint32_t s1 = *(uint32_t*)arg;
	uint32_t s2 = ((context_arg*)obj)->ping_key;
	return s1 != s2;
}

void on_socket_ready (uv_poll_t *req, int status, int events) {
	context_arg *carg = req->data;
	packet_t pckt;
	icmp_packet_t *i_p;

	if (events & UV_WRITABLE) {
		socket_write_mode(carg, 0);

			struct sockaddr *sa = (struct sockaddr *)&carg->ping_addr;
			socklen_t slen = carg->ping_addr_len ? carg->ping_addr_len : (socklen_t)sizeof(struct sockaddr_in);
			size_t plen = icmp_send_len(carg);
			i_p = &pckt.icmp_req;

			// prepare icmp packet
			bzero(i_p, plen);
			i_p->hdr.type = carg->ping_ipv6 ? ICMP6_ECHO_REQUEST : ICMP_ECHO;
			i_p->hdr.un.echo.id = carg->packets_id;
			i_p->hdr.un.echo.sequence = carg->sequence_id;
			i_p->ts = get_monotonic_time();
			if (!carg->ping_ipv6)
				i_p->hdr.checksum = checksum(i_p, (int)plen);

			carg->ping_key = i_p->hdr.un.echo.id;
			context_arg *test_carg = alligator_ht_search(ac->ping_hash, ping_struct_compare, &carg->ping_key, tommy_inthash_u32(carg->ping_key));
			if (!test_carg)
			{
				carglog(carg, L_DEBUG, "icmp: register echo id=%"PRIu32" host=%s carg=%p\n", carg->ping_key, carg->host, carg);
				alligator_ht_insert(ac->ping_hash, &(carg->ping_node), carg, tommy_inthash_u32(carg->ping_key));
			}

			if ( sendto(carg->fd, i_p, plen, 0, sa, slen) <= 0 ) {
				icmp_stop_run(carg);
				carglog(carg, L_ERROR, "icmp: sendto failed: %s\n", strerror(errno));
				return;
			}
			carg->check_receive = 1;
			++carg->sequence_id;

			uv_timer_stop(&carg->t_towrite);

			if (carg->sequence_id < carg->pingloop) {
				uv_timer_start(&carg->t_seq_timer, on_seq_timer, carg->sequence_time, 0);
			} else {
				uv_timer_start(&carg->t_timeout, on_timeout, carg->timeout_time, 0);
			}
	}
	
	if (events & UV_READABLE) {
		struct sockaddr_storage r_addr;
		socklen_t len = sizeof(r_addr);
		size_t size;
		void *recv_buf;
		size_t recv_cap;

		if (carg->ping_ipv6) {
			recv_buf = &pckt.icmp_req;
			recv_cap = sizeof(pckt.icmp_req);
		} else {
			recv_buf = &pckt;
			recv_cap = sizeof(pckt);
		}

		if ((size = recvfrom(carg->fd, recv_buf, recv_cap, 0, (struct sockaddr *)&r_addr, &len)) > 0 ) {
			i_p = carg->ping_ipv6 ? &pckt.icmp_req : &pckt.icmp_res.pckt;
			uint32_t key = i_p->hdr.un.echo.id;

			context_arg *rcarg = alligator_ht_search(ac->ping_hash, ping_struct_compare, &key, tommy_inthash_u32(key));
			if (!rcarg)
				return;

			carglog(rcarg, L_DEBUG, "icmp: lookup echo id=%"PRIu32" host=%s carg=%p\n", key, rcarg->host, rcarg);
			dump_packet(rcarg, i_p);

			uint8_t echo_type = rcarg->ping_ipv6 ? ICMP6_ECHO_REQUEST : ICMP_ECHO;
			uint8_t reply_ok = rcarg->ping_ipv6 ? (i_p->hdr.type == ICMP6_ECHO_REPLY) : (i_p->hdr.type != ICMP_ECHO);
			if (i_p->hdr.un.echo.id == rcarg->packets_id && i_p->hdr.type != echo_type && reply_ok && rcarg->check_receive) {
				int match = 0;
				if (rcarg->ping_ipv6 && r_addr.ss_family == AF_INET6) {
					struct sockaddr_in6 *ra = (struct sockaddr_in6 *)&r_addr;
					struct sockaddr_in6 *dst = (struct sockaddr_in6 *)&rcarg->ping_addr;
					match = IN6_ARE_ADDR_EQUAL(&ra->sin6_addr, &dst->sin6_addr);
				} else if (!rcarg->ping_ipv6 && r_addr.ss_family == AF_INET) {
					struct sockaddr_in *ra = (struct sockaddr_in *)&r_addr;
					struct sockaddr_in *dst = (struct sockaddr_in *)&rcarg->ping_addr;
					match = ra->sin_addr.s_addr == dst->sin_addr.s_addr;
				}
				if (match) {
					if (!rcarg->check_receive) {
						rcarg->icmp_duplicates++;
						return;
					}
					rcarg->check_receive = 0;
					carglog(rcarg, L_DEBUG, "icmp: matching reply from expected peer, emit sample\n");
					int hop = -1;
					if (!rcarg->ping_ipv6)
						hop = pckt.icmp_res.iphdr.ip_ttl;
					icmp_emit_one(rcarg, i_p, hop);
				}
			}
		}
	}
}


void icmp_client_repeat_period(uv_timer_t *timer);
void icmp_start(void *arg)
{
	context_arg *carg = arg;
	if (!carg)
		return;

	if (carg->lock)
		return;

	carg->loop = get_threaded_loop_t_or_default(carg->threaded_loop_name);
	if (carg->period && !carg->read_counter) {
		carg->period_timer = alligator_cache_get(ac->uv_cache_timer, sizeof(uv_timer_t));
		carg->period_timer->data = carg;
		uv_timer_init(carg->loop, carg->period_timer);
		uv_timer_start(carg->period_timer, icmp_client_repeat_period, carg->period, 0);
	}

	carg->lock = 1;
	// reset runtime data
	carg->sequence_id = 0;
	carg->sequence_done = 0;
	carg->sequence_success = 0;
	carg->sequence_error = 0;

	// init raw socket
	int ttl_val = carg->icmp_ttl > 0 ? carg->icmp_ttl : 64;
	int tos_val = carg->icmp_tos;
	int sd;
	if (carg->ping_ipv6)
		sd = socket(PF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
	else
		sd = socket(PF_INET, SOCK_RAW, IPPROTO_ICMP);
	if ( sd < 0 )
	{
		carglog(carg, L_ERROR, "open raw socket error\n");
		carg->lock = 0;
		return;
	}
	if (carg->ping_ipv6) {
		if ( setsockopt(sd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &ttl_val, sizeof(ttl_val)) != 0) {
			carglog(carg, L_ERROR, "set hop limit option error\n");
			close(sd);
			carg->lock = 0;
			return;
		}
		if (tos_val)
			setsockopt(sd, IPPROTO_IPV6, IPV6_TCLASS, &tos_val, sizeof(tos_val));
	} else if ( setsockopt(sd, SOL_IP, IP_TTL, &ttl_val, sizeof(ttl_val)) != 0) {
		carglog(carg, L_ERROR, "set TTL option error\n");
		close(sd);
		carg->lock = 0;
		return;
	}
	if (!carg->ping_ipv6 && tos_val)
		setsockopt(sd, SOL_IP, IP_TOS, &tos_val, sizeof(tos_val));
	if (carg->bind_address && carg->bind_address[0]) {
		char ip[INET6_ADDRSTRLEN];
		const char *src = carg->bind_address;
		if (*src == '[') {
			const char *rb = strchr(src, ']');
			size_t n;
			if (!rb) {
				carglog(carg, L_ERROR, "icmp: invalid source_ip_address '%s'\n", carg->bind_address);
				close(sd);
				carg->lock = 0;
				return;
			}
			n = (size_t)(rb - src - 1);
			if (n >= sizeof(ip))
				n = sizeof(ip) - 1;
			memcpy(ip, src + 1, n);
			ip[n] = 0;
			src = ip;
		}
		if (carg->ping_ipv6) {
			struct sockaddr_in6 a;
			memset(&a, 0, sizeof(a));
			a.sin6_family = AF_INET6;
			if (inet_pton(AF_INET6, src, &a.sin6_addr) != 1) {
				carglog(carg, L_ERROR, "icmp: source_ip_address '%s' is not a valid IPv6 address\n", carg->bind_address);
				close(sd);
				carg->lock = 0;
				return;
			}
			if (bind(sd, (struct sockaddr *)&a, sizeof(a)) != 0) {
				carglog(carg, L_ERROR, "icmp: bind source_ip_address '%s' failed: %s\n", carg->bind_address, strerror(errno));
				close(sd);
				carg->lock = 0;
				return;
			}
		} else {
			struct sockaddr_in a;
			memset(&a, 0, sizeof(a));
			a.sin_family = AF_INET;
			if (inet_pton(AF_INET, src, &a.sin_addr) != 1) {
				carglog(carg, L_ERROR, "icmp: source_ip_address '%s' is not a valid IPv4 address\n", carg->bind_address);
				close(sd);
				carg->lock = 0;
				return;
			}
			if (bind(sd, (struct sockaddr *)&a, sizeof(a)) != 0) {
				carglog(carg, L_ERROR, "icmp: bind source_ip_address '%s' failed: %s\n", carg->bind_address, strerror(errno));
				close(sd);
				carg->lock = 0;
				return;
			}
		}
		carglog(carg, L_INFO, "icmp: bound source_ip_address %s\n", carg->bind_address);
	}
	if ( fcntl(sd, F_SETFL, O_NONBLOCK) != 0 ) {
		carglog(carg, L_ERROR, "sequest nonblocking I/O error\n");
		close(sd);
		carg->lock = 0;
		return;
	}

	carg->fd = sd;

	uv_timer_init(carg->loop, &carg->t_timeout);
	uv_timer_init(carg->loop, &carg->t_towrite);
	uv_timer_init(carg->loop, &carg->t_seq_timer);

	uv_poll_init(carg->loop, &carg->poll_socket, sd);
	carg->poll_socket.data = carg;
	carg->t_towrite.data = carg;
	carg->t_seq_timer.data = carg;
	carg->t_timeout.data = carg;

	uv_timer_start(&carg->t_towrite, on_towrite, icmp_splay_ms(carg), carg->packets_send_period);
	carg->total_time = setrtime();
}

void for_icmp_client_connect(void *arg)
{
	context_arg *carg = arg;
	if (carg->period && carg->read_counter)
		return;

	icmp_start(arg);
}

void icmp_client_repeat_period(uv_timer_t *timer)
{
	context_arg *carg = timer->data;
	if (!carg->period)
		return;

	icmp_start((void*)carg);
}

void icmp_resolved(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res)
{
	context_arg *carg = resolver->data;
	free(resolver);
	carg->resolve_time_finish = setrtime();

	if (status == -1 || !res) {
		carglog(carg, L_ERROR, "getaddrinfo callback error\n");
		return;
	}

	char addr[INET6_ADDRSTRLEN] = {'\0'};
	memset(&carg->ping_addr, 0, sizeof(carg->ping_addr));
	if (res->ai_family == AF_INET6) {
		carg->ping_ipv6 = 1;
		carg->ping_addr_len = (socklen_t)res->ai_addrlen;
		if (carg->ping_addr_len > sizeof(carg->ping_addr))
			carg->ping_addr_len = sizeof(carg->ping_addr);
		memcpy(&carg->ping_addr, res->ai_addr, carg->ping_addr_len);
		uv_ip6_name((struct sockaddr_in6 *)res->ai_addr, addr, sizeof(addr));
	} else {
		carg->ping_ipv6 = 0;
		carg->ping_addr_len = sizeof(struct sockaddr_in);
		memcpy(&carg->ping_addr, res->ai_addr, sizeof(struct sockaddr_in));
		memcpy(&carg->remote_addr, res->ai_addr, sizeof(struct sockaddr_in));
		uv_ip4_name((struct sockaddr_in *)res->ai_addr, addr, 16);
	}
	if (!carg->key) {
		carg->key = malloc(64);
		if (carg->key)
			snprintf(carg->key, 64, "%s:%u:%d", addr, 0, res->ai_family);
	}

	alligator_ht_insert(ac->iggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
	uv_freeaddrinfo(res);

	icmp_start(carg);
}

char* icmp_client(context_arg *carg)
{
	//carg->sequence_size = 100;
	carg->packets_send_period = ac->iggregator_repeat;
	carg->timeout_time = ac->iggregator_repeat - 100; // timeout all probes
	carg->sequence_time = carg->timeout; // timeout each probe
	if (carg->icmp_interval_ms) {
		carg->icmp_continuous = 1;
		carg->packets_send_period = carg->icmp_interval_ms;
		carg->pingloop = 1;
		carg->timeout_time = carg->icmp_interval_ms * 4;
		if (carg->timeout_time < 1000)
			carg->timeout_time = 1000;
	}
	carg->packets_id = getpid() + ac->ping_id++;

	uv_getaddrinfo_t *resolver = malloc(sizeof(*resolver));
	resolver->data = carg;
	carg->resolve_time = setrtime();
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = probe_ip_family(carg->host, carg->ip_version);
	hints.ai_socktype = 0;
	int r = uv_getaddrinfo(carg->loop, resolver, icmp_resolved, carg->host, NULL, &hints);
	if (r)
	{
		carglog(carg, L_ERROR, "%s\n", uv_strerror(r));
		return NULL;
	}
	else
	{
		++(ac->icmp_client_count);
	}
	return "icmp";
}

//int main()
//{
//	ping_id = 0;
//	ping_hash = calloc(1, sizeof(alligator_ht));
//	alligator_ht_init(ping_hash);
//
//	icmp_run_ping("192.168.1.1");
//	return uv_run(uv_default_loop(), UV_RUN_DEFAULT);
//}

static void icmp_timer_cb(uv_timer_t* handle) {
	alligator_ht_foreach(ac->iggregator, for_icmp_client_connect);
}

void icmp_client_handler()
{
	//uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	uv_timer_init(ac->loop, &ac->icmp_client_timer);
	uv_timer_start(&ac->icmp_client_timer, icmp_timer_cb, ac->iggregator_startup, ac->iggregator_repeat);
}

void icmp_client_del(context_arg *carg)
{
	if (!carg)
		return;

	if (carg)
	{
		if (carg->remove_from_hash)
			alligator_ht_remove_existing(ac->aggregators, &(carg->context_node));
		
		carg->lock = 1;
		alligator_ht_remove_existing(ac->iggregator, &(carg->node));
		(ac->icmp_client_count)--;
		if (carg->parser_handler != blackbox_null)
			free(carg->data);
		carg_free(carg);
	}
}
#else
char *icmp_client(context_arg *carg)
{
	(void)carg;
	return NULL;
}

void icmp_client_del(context_arg *carg)
{
	(void)carg;
}

void icmp_client_handler(void)
{
}

void icmp_start(void *arg)
{
	(void)arg;
}
#endif
