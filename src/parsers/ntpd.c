#include <stdio.h>
#include "metric/namespace.h"
#include "events/context_arg.h"
#include "main.h"

  typedef struct ntp_packet
  {

    uint8_t li_vn_mode;      // Eight bits. li, vn, and mode.
                             // li.   Two bits.   Leap indicator.
                             // vn.   Three bits. Version number of the protocol.
                             // mode. Three bits. Client will pick mode 3 for client.

    uint8_t stratum;         // Eight bits. Stratum level of the local clock.
    uint8_t poll;            // Eight bits. Maximum interval between successive messages.
    uint8_t precision;       // Eight bits. Precision of the local clock.

    uint32_t rootDelay;      // 32 bits. Total round trip delay time.
    uint32_t rootDispersion; // 32 bits. Max error aloud from primary clock source.
    uint32_t refId;          // 32 bits. Reference clock identifier.

    uint32_t refTm_s;        // 32 bits. Reference time-stamp seconds.
    uint32_t refTm_f;        // 32 bits. Reference time-stamp fraction of a second.

    uint32_t origTm_s;       // 32 bits. Originate time-stamp seconds.
    uint32_t origTm_f;       // 32 bits. Originate time-stamp fraction of a second.

    uint32_t rxTm_s;         // 32 bits. Received time-stamp seconds.
    uint32_t rxTm_f;         // 32 bits. Received time-stamp fraction of a second.

    uint32_t txTm_s;         // 32 bits and the most important field the client cares about. Transmit time-stamp seconds.
    uint32_t txTm_f;         // 32 bits. Transmit time-stamp fraction of a second.

  } ntp_packet;              // Total: 384 bits or 48 bytes.

double ntp_rtt(double origin_time, uint64_t received_time, uint64_t rcv_fraction, uint64_t transmit_time, uint64_t trm_fraction, double ntpTime)
{
	double a = ntpTime - origin_time;
	double rcv_f = (double)ntohl(rcv_fraction) / 0x100000000L;
	double trm_f = (double)ntohl(trm_fraction) / 0x100000000L;
	double b = (transmit_time + trm_f) - (received_time + rcv_f);
	double rtt = a - b;
	if (rtt < 0)
	{
		rtt = 0;
	}

	return rtt;
}

uint32_t ntp_rootDistance(uint64_t rtt, uint32_t rootDelay, uint32_t rootDispersion)
{
	uint32_t totalDelay = rtt + rootDelay;
	return totalDelay/2 + rootDispersion;
}

double ntp_short_duration(uint32_t t)
{
	uint64_t sec = (uint64_t)(t>>16) * 1000000000;
	uint64_t frac = (uint64_t)(t&0xffff) * 1000000000;
	uint64_t nsec = frac >> 16;
	if ((uint16_t)frac >= 0x8000) {
		++nsec;
	}

	return (sec + nsec);
}

void ntp_handler(char *ntpData, size_t size, context_arg *carg)
{

	r_time now = setrtime();
	double nowtime = (now.sec * 1.0) + ((double)(now.nsec) * 1.0 / 1000000000);
	ntp_packet *ntpp = (ntp_packet*)ntpData;
	uint32_t txTm_s = ntohl( ntpp->txTm_s ); 
	uint32_t txTm_f = ntohl( ntpp->txTm_f );
	uint64_t txTm = ( time_t ) ( txTm_s - 2208988800ull );
	double diff = nowtime - (((double)(txTm) * 1.00) + ((double)(txTm_f)/0x100000000L));

	if (carg->log_level)
		printf("txTm %"PRIu64", nowtime %lf, diff %lf\n", txTm, nowtime, diff);

	metric_add_auto("ntp_drift_seconds", &diff, DATATYPE_DOUBLE, carg);

	uint64_t stratum = ntpp->stratum;
	metric_add_auto("ntp_stratum", &stratum, DATATYPE_UINT, carg);

	uint64_t leap = ntpp->li_vn_mode & (1<<0);
	metric_add_auto("ntp_leap", &leap, DATATYPE_UINT, carg);

	uint64_t rootDispersion = ntohl(ntpp->rootDispersion);
	metric_add_auto("ntp_root_dispersion_seconds", &rootDispersion, DATATYPE_UINT, carg);

	uint64_t rootDelay = ntohl(ntpp->rootDelay);
	metric_add_auto("ntp_root_delay_seconds", &rootDelay, DATATYPE_UINT, carg);

	uint64_t precision = ntpp->precision;
	metric_add_auto("ntp_precision_seconds", &precision, DATATYPE_UINT, carg);

	double refTm_s = ntohl(ntpp->refTm_s) + ((double)ntohl(ntpp->refTm_f)/0x100000000L);
	metric_add_auto("ntp_reference_timestamp_seconds", &refTm_s, DATATYPE_DOUBLE, carg);

	double origin_time = (carg->write_time_finish.sec * 1.0) + ((double)(carg->write_time_finish.nsec) * 1.0 / 1000000000);
	double rtt = ntp_rtt(origin_time, ntpp->rxTm_s, ntpp->rxTm_f, ntpp->txTm_s, ntpp->txTm_f, nowtime);
	metric_add_auto("ntp_rtt_seconds", &rtt, DATATYPE_DOUBLE, carg);

	uint64_t root_distance = ntp_rootDistance(rtt, rootDelay, rootDispersion);
	metric_add_auto("ntp_root_distance_seconds", &root_distance, DATATYPE_UINT, carg);

	carg->parser_status = 1;
}

string* ntp_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	char data[48];
	memset(data, 0, 48);
	data[0] = 0x1B;
	return string_init_alloc(data, 48);
}

void ntp_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));

	actx->key = strdup("ntp");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);

	actx->handler[0].name = ntp_handler;
	actx->handler[0].mesg_func = ntp_mesg;
	strlcpy(actx->handler[0].key,"ntp", 255);

	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
