#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <arpa/inet.h>
#include "metric/namespace.h"
#include "metric/metric_types.h"
#include "events/context_arg.h"
#include "common/selector.h"
#include "common/url.h"
#include "main.h"

/* Chrony command-and-monitoring protocol (candm.h), version 6.
 * REQ_TRACKING is a padded datagram: header (20) + padding so the request
 * is as long as the tracking reply (104). */
#define CHRONY_PROTO_VERSION 6
#define CHRONY_PKT_REQUEST 1
#define CHRONY_PKT_REPLY 2
#define CHRONY_REQ_TRACKING 33
#define CHRONY_RPY_TRACKING 5
#define CHRONY_STT_SUCCESS 0
#define CHRONY_CMD_HEADER 20
#define CHRONY_RPY_HEADER 28
#define CHRONY_TRACKING_LEN 104
#define CHRONY_FLOAT_EXP_BITS 7
#define CHRONY_FLOAT_COEF_BITS 25

static void chrony_metric_help(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "chrony_last_offset_seconds", METRIC_TYPE_GAUGE, "Chrony last offset from NTP in seconds.");
	namespace_metric_family_set(NULL, carg, "chrony_rms_offset_seconds", METRIC_TYPE_GAUGE, "Chrony RMS offset in seconds.");
	namespace_metric_family_set(NULL, carg, "chrony_frequency_ppm", METRIC_TYPE_GAUGE, "Chrony frequency error in ppm.");
	namespace_metric_family_set(NULL, carg, "chrony_skew_ppm", METRIC_TYPE_GAUGE, "Chrony estimated error bound of frequency in ppm.");
	namespace_metric_family_set(NULL, carg, "chrony_root_delay_seconds", METRIC_TYPE_GAUGE, "Chrony root delay in seconds.");
	namespace_metric_family_set(NULL, carg, "chrony_root_dispersion_seconds", METRIC_TYPE_GAUGE, "Chrony root dispersion in seconds.");
	namespace_metric_family_set(NULL, carg, "chrony_stratum", METRIC_TYPE_GAUGE, "Chrony stratum.");
	namespace_metric_family_set(NULL, carg, "chrony_update_interval_seconds", METRIC_TYPE_GAUGE, "Chrony update interval in seconds.");
}

static void chrony_emit(context_arg *carg, uint64_t stratum, double last_offset, double rms_offset,
	double freq, double skew, double root_delay, double root_disp, double update_interval)
{
	chrony_metric_help(carg);
	metric_add_auto("chrony_last_offset_seconds", &last_offset, DATATYPE_DOUBLE, carg);
	metric_add_auto("chrony_rms_offset_seconds", &rms_offset, DATATYPE_DOUBLE, carg);
	metric_add_auto("chrony_frequency_ppm", &freq, DATATYPE_DOUBLE, carg);
	metric_add_auto("chrony_skew_ppm", &skew, DATATYPE_DOUBLE, carg);
	metric_add_auto("chrony_root_delay_seconds", &root_delay, DATATYPE_DOUBLE, carg);
	metric_add_auto("chrony_root_dispersion_seconds", &root_disp, DATATYPE_DOUBLE, carg);
	metric_add_auto("chrony_stratum", &stratum, DATATYPE_UINT, carg);
	metric_add_auto("chrony_update_interval_seconds", &update_interval, DATATYPE_DOUBLE, carg);
}

static uint16_t chrony_read_u16(const unsigned char *p)
{
	uint16_t v;
	memcpy(&v, p, sizeof(v));
	return ntohs(v);
}

static uint32_t chrony_read_u32(const unsigned char *p)
{
	uint32_t v;
	memcpy(&v, p, sizeof(v));
	return ntohl(v);
}

static double chrony_float_ntoh(const unsigned char *p)
{
	uint32_t x = chrony_read_u32(p);
	int32_t exp = (int32_t)(x >> CHRONY_FLOAT_COEF_BITS);
	int32_t coef;

	if (exp >= (1 << (CHRONY_FLOAT_EXP_BITS - 1)))
		exp -= (1 << CHRONY_FLOAT_EXP_BITS);
	exp -= CHRONY_FLOAT_COEF_BITS;

	coef = (int32_t)(x % (1U << CHRONY_FLOAT_COEF_BITS));
	if (coef >= (1 << (CHRONY_FLOAT_COEF_BITS - 1)))
		coef -= (1 << CHRONY_FLOAT_COEF_BITS);

	return coef * pow(2.0, exp);
}

static int chrony_parse_tracking_cmd(const unsigned char *pkt, size_t size, context_arg *carg)
{
	uint64_t stratum;
	double last_offset, rms_offset, freq, skew, root_delay, root_disp, update_interval;

	if (size < CHRONY_TRACKING_LEN)
		return 0;
	if (pkt[1] != CHRONY_PKT_REPLY)
		return 0;
	if (chrony_read_u16(pkt + 6) != CHRONY_RPY_TRACKING)
		return 0;
	if (chrony_read_u16(pkt + 8) != CHRONY_STT_SUCCESS)
		return 0;

	/* RPY_Tracking after 28-byte reply header: ref_id(4) IPAddr(20)
	 * stratum(2) leap(2) Timespec(12) then floats. */
	stratum = chrony_read_u16(pkt + CHRONY_RPY_HEADER + 24);
	last_offset = chrony_float_ntoh(pkt + CHRONY_RPY_HEADER + 44);
	rms_offset = chrony_float_ntoh(pkt + CHRONY_RPY_HEADER + 48);
	freq = chrony_float_ntoh(pkt + CHRONY_RPY_HEADER + 52);
	skew = chrony_float_ntoh(pkt + CHRONY_RPY_HEADER + 60);
	root_delay = chrony_float_ntoh(pkt + CHRONY_RPY_HEADER + 64);
	root_disp = chrony_float_ntoh(pkt + CHRONY_RPY_HEADER + 68);
	update_interval = chrony_float_ntoh(pkt + CHRONY_RPY_HEADER + 72);

	chrony_emit(carg, stratum, last_offset, rms_offset, freq, skew, root_delay, root_disp, update_interval);
	return 1;
}

static int chrony_parse_tracking_text(char *copy, context_arg *carg)
{
	double last_offset = 0, rms_offset = 0, freq = 0, skew = 0;
	double root_delay = 0, root_disp = 0, update_interval = 0;
	uint64_t stratum = 0;
	int got = 0;

	char *save = NULL;
	for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		char *colon = strchr(line, ':');
		if (!colon)
			continue;
		*colon = '\0';
		char *val = colon + 1;
		val += strspn(val, " \t");
		if (strstr(line, "Stratum")) {
			stratum = strtoull(val, NULL, 10);
			got = 1;
		} else if (strstr(line, "Last offset")) {
			last_offset = atof(val);
			got = 1;
		} else if (strstr(line, "RMS offset")) {
			rms_offset = atof(val);
			got = 1;
		} else if (strstr(line, "Frequency")) {
			freq = atof(val);
			if (strstr(val, "slow"))
				freq = -freq;
			got = 1;
		} else if (strstr(line, "Skew")) {
			skew = atof(val);
			got = 1;
		} else if (strstr(line, "Root delay")) {
			root_delay = atof(val);
			got = 1;
		} else if (strstr(line, "Root dispersion")) {
			root_disp = atof(val);
			got = 1;
		} else if (strstr(line, "Update interval")) {
			update_interval = atof(val);
			got = 1;
		}
	}

	if (!got)
		return 0;

	chrony_emit(carg, stratum, last_offset, rms_offset, freq, skew, root_delay, root_disp, update_interval);
	return 1;
}

void chrony_handler(char *metrics, size_t size, context_arg *carg)
{
	if (!metrics || !size) {
		carg->parser_status = 0;
		return;
	}

	if ((unsigned char)metrics[1] == CHRONY_PKT_REPLY) {
		carg->parser_status = chrony_parse_tracking_cmd((const unsigned char *)metrics, size, carg) ? 1 : 0;
		return;
	}

	char *copy = strndup(metrics, size);
	if (!copy) {
		carg->parser_status = 0;
		return;
	}
	int ok = chrony_parse_tracking_text(copy, carg);
	free(copy);
	carg->parser_status = ok ? 1 : 0;
}

string* chrony_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
{
	unsigned char *pkt;
	uint16_t cmd, attempt;
	uint32_t seq, z;

	(void)arg;
	(void)env;
	(void)proxy_settings;

	if (!hi || (hi->transport != APROTO_UNIXGRAM && hi->proto != APROTO_UDP))
		return NULL;

	/* +1: string_init_add writes a NUL at s[len]. */
	pkt = calloc(1, CHRONY_TRACKING_LEN + 1);
	if (!pkt)
		return NULL;

	pkt[0] = CHRONY_PROTO_VERSION;
	pkt[1] = CHRONY_PKT_REQUEST;
	cmd = htons(CHRONY_REQ_TRACKING);
	memcpy(pkt + 4, &cmd, sizeof(cmd));
	attempt = htons(0);
	memcpy(pkt + 6, &attempt, sizeof(attempt));
	seq = htonl(1);
	memcpy(pkt + 8, &seq, sizeof(seq));
	z = 0;
	memcpy(pkt + 12, &z, sizeof(z));
	memcpy(pkt + 16, &z, sizeof(z));

	return string_init_add((char *)pkt, CHRONY_TRACKING_LEN, CHRONY_TRACKING_LEN);
}

void chrony_parser_push()
{
	aggregate_context *actx = calloc(1, sizeof(*actx));
	actx->key = strdup("chrony");
	actx->handlers = 1;
	actx->handler = calloc(1, sizeof(*actx->handler) * actx->handlers);
	actx->handler[0].name = chrony_handler;
	actx->handler[0].mesg_func = chrony_mesg;
	strlcpy(actx->handler[0].key, "chrony", 255);
	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
}
