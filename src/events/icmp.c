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
#include <unistd.h>
#include <inttypes.h>
#include <netinet/in.h>
#include "dstructures/tommy.h"
#include "common/logs.h"
#include "common/rtime.h"
#include "probe/probe.h"
#include "parsers/multiparser.h"
#include "main.h"
#define PACKETSIZE 64

extern aconf *ac;

typedef struct {
	struct icmphdr hdr;
	uint32_t ts;
	uint8_t payload[PACKETSIZE - sizeof(struct icmphdr) - sizeof(uint32_t)];
} icmp_packet_t;

typedef union packet_u {
	uint8_t raw[PACKETSIZE];
	icmp_packet_t icmp_req;
	struct {
		struct ip iphdr;
		icmp_packet_t pckt;
	} icmp_res;
} packet_t;
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

	return (ts.tv_sec / 1000 + ts.tv_nsec / 1000000);
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

	carglog(carg, L_INFO, "host %s success %"PRIu64", error %"PRIu64", percent success: %lf, percent error: %lf, resolve time: %"PRIu64", read time: %"PRIu64", total time: %"PRIu64", tries %"PRIu64"\n", carg->host, carg->sequence_success, carg->sequence_error, success_percent, error_percent, resolve_time, read_time, total_time, carg->pingloop);

	metric_add_labels2("aggregator_read_time", &read_time, DATATYPE_UINT, carg, "type", "icmp", "host", carg->host);
	//metric_add_labels2("aggregator_resolve_time", &resolve_time, DATATYPE_UINT, carg, "type", "icmp", "host", carg->host);
	metric_add_labels2("aggregator_packet_received", &carg->sequence_success, DATATYPE_UINT, carg, "type", "icmp", "host", carg->host);
	metric_add_labels2("aggregator_packet_received_percent", &success_percent, DATATYPE_DOUBLE, carg, "type", "icmp", "host", carg->host);
	metric_add_labels2("aggregator_packet_loss", &carg->sequence_error, DATATYPE_UINT, carg, "type", "icmp", "host", carg->host);
	metric_add_labels2("aggregator_packet_loss_percent", &error_percent, DATATYPE_DOUBLE, carg, "type", "icmp", "host", carg->host);

	if (carg->data && carg->parser_handler == blackbox_null)
	{
		probe_node *pn = carg->data;
		uint64_t val = 0;
		if (success_percent >= carg->pingpercent_success)
			val = 1;

		metric_add_labels6("probe_success", &val, DATATYPE_UINT, carg, "proto", "icmp", "type", "blackbox", "host", carg->host, "key", carg->key, "prober", pn->prober_str, "module", pn->name);
	}
}

int icmp_stop (context_arg *carg) {
	if (carg->lock) {
		icmp_stop_run(carg);
	}

	return 1;
}

void icmp_emit_one (context_arg* carg, icmp_packet_t* icmp)
{
	uint32_t ts = get_monotonic_time();
	carg->read_time_finish = setrtime();
	uint32_t icmp_code_id = icmp->hdr.code;
	uint32_t icmp_type_id = icmp->hdr.type;
	uint32_t time_diff = get_monotonic_time_diff(icmp->ts, ts);
	uint64_t read_time = getrtime_mcs(carg->read_time, carg->read_time_finish, 0);
	uint64_t resolve_time = getrtime_mcs(carg->resolve_time, carg->resolve_time_finish, 0);

	++carg->sequence_done;
	++carg->sequence_success;
	uv_timer_stop(&carg->t_seq_timer);
	carglog(carg, L_INFO, "host %s, code id: %"PRIu32",  type id: %"PRIu32", time diff: %"PRIu32", resolve_time: %"PRIu64", read_time: %"PRIu64", done/size %"PRIu64"/%"PRIu64"\n", carg->host, icmp_type_id, icmp_code_id, time_diff, resolve_time, read_time, carg->sequence_done, carg->pingloop);
	if (carg->sequence_done >= carg->pingloop) {
		carglog(carg, L_INFO, "host %s success %d, error %d\n", carg->host, carg->sequence_success, carg->sequence_error);
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
	carglog(carg, L_INFO, "host timeout: %s\n", carg->host);
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
	carglog(carg, L_INFO, "host sequence: %s: %"PRIu64"\n", carg->host, carg->sequence_done);
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

			struct sockaddr *sa = (struct sockaddr *)&carg->dest;
			i_p = &pckt.icmp_req;

			// prepare icmp packet
			bzero(i_p, sizeof(*i_p));
			i_p->hdr.type = ICMP_ECHO;
			i_p->hdr.un.echo.id = carg->packets_id;
			i_p->hdr.un.echo.sequence = carg->sequence_id;
			i_p->ts = get_monotonic_time();
			i_p->hdr.checksum = checksum(i_p, sizeof(*i_p));

			carg->ping_key = i_p->hdr.un.echo.id;
			context_arg *test_carg = alligator_ht_search(ac->ping_hash, ping_struct_compare, &carg->ping_key, tommy_inthash_u32(carg->ping_key));
			if (!test_carg)
			{
				carglog(carg, L_INFO, "echo id %s:%p: %"PRIu32"\n", carg->host, carg, carg->ping_key);
				alligator_ht_insert(ac->ping_hash, &(carg->ping_node), carg, tommy_inthash_u32(carg->ping_key));
			}

			if ( sendto(carg->fd, i_p, sizeof(*i_p), 0, (const struct sockaddr *)&carg->dest, sizeof(*sa)) <= 0 ) {
				icmp_stop_run(carg);
				perror("sendto");
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
		struct sockaddr r_addr;
		struct sockaddr_in *sa1;
		socklen_t len = sizeof(r_addr);
		size_t size;

		if ((size = recvfrom(carg->fd, &pckt, sizeof(pckt), 0, &r_addr, &len)) > 0 ) {
			i_p = &pckt.icmp_res.pckt;
			sa1 = (struct sockaddr_in *) &r_addr;
			uint32_t key = i_p->hdr.un.echo.id;

			context_arg *carg = alligator_ht_search(ac->ping_hash, ping_struct_compare, &key, tommy_inthash_u32(key));
			if (!carg)
				return;

			carglog(carg, L_INFO, "echo get id %s:%p: %"PRIu32"\n", carg->host, carg, key);
			dump_packet(carg, i_p);

			if (i_p->hdr.un.echo.id == carg->packets_id && i_p->hdr.type != ICMP_ECHO && carg->check_receive) {
				// it's our packets, lets see what within
				//sa2 = (struct sockaddr_in *) &carg->dest;
				if ( sa1->sin_addr.s_addr == carg->dest.sin_addr.s_addr ) {
					carg->check_receive = 0;
					carglog(carg, L_DEBUG,	"======");
					icmp_emit_one(carg, i_p);
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
	int ttl_val = 64;
	int sd = socket(PF_INET, SOCK_RAW, IPPROTO_ICMP);
	if ( sd < 0 )
	{
		carglog(carg, L_ERROR, "open raw socket error\n");
		carg->lock = 0;
		return;
	}
	if ( setsockopt(sd, SOL_IP, IP_TTL, &ttl_val, sizeof(ttl_val)) != 0) {
		carglog(carg, L_ERROR, "set TTL option error\n");
		carg->lock = 0;
		return;
	}
	if ( fcntl(sd, F_SETFL, O_NONBLOCK) != 0 ) {
		carglog(carg, L_ERROR, "sequest nonblocking I/O error\n");
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

	uv_timer_start(&carg->t_towrite, on_towrite, 0, carg->packets_send_period);
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

	char addr[17] = {'\0'};
	uv_ip4_name((struct sockaddr_in*)res->ai_addr, addr, 16);
	memcpy(&carg->dest, (struct sockaddr_in*)res->ai_addr, sizeof(struct sockaddr_in));
	if (!carg->key)
		carg->key = malloc(64);
	snprintf(carg->key, 64, "%s:%u:%d", addr, carg->dest.sin_port, carg->dest.sin_family);

	alligator_ht_insert(ac->iggregator, &(carg->node), carg, tommy_strhash_u32(0, carg->key));
}

char* icmp_client(context_arg *carg)
{
	//carg->sequence_size = 100;
	carg->packets_send_period = ac->iggregator_repeat;
	carg->timeout_time = ac->iggregator_repeat - 100; // timeout all probes
	carg->sequence_time = carg->timeout; // timeout each probe
	carg->packets_id = getpid() + ac->ping_id++;

	uv_getaddrinfo_t *resolver = malloc(sizeof(*resolver));
	resolver->data = carg;
	carg->resolve_time = setrtime();
	int r = uv_getaddrinfo(carg->loop, resolver, icmp_resolved, carg->host, 0, NULL);
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
		free(carg->data);
		carg_free(carg);
	}
}
#endif
#ifdef __FreeBSD__
char* icmp_client(context_arg *carg) {return NULL;}
void icmp_client_del(context_arg *carg) {}
#endif
