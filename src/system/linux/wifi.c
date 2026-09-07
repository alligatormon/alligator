#ifdef __linux__

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>

#ifndef NLA_TYPE_MASK
#define NLA_TYPE_MASK 0x3fff
#endif

#include "main.h"
#include "common/logs.h"
#include "system/linux/wifi.h"

extern aconf *ac;

#ifndef NL80211_GENL_NAME
#define NL80211_GENL_NAME "nl80211"
#endif
#ifndef NL80211_GENL_VERSION
#define NL80211_GENL_VERSION 1
#endif

/* Stable UAPI numbers from linux/nl80211.h (append-only). */
enum {
	NL80211_CMD_GET_INTERFACE_LOCAL = 5,
	NL80211_CMD_GET_STATION_LOCAL = 17,
	NL80211_CMD_GET_SCAN_LOCAL = 32,
};

enum {
	NL80211_ATTR_IFINDEX_LOCAL = 3,
	NL80211_ATTR_IFNAME_LOCAL = 4,
	NL80211_ATTR_IFTYPE_LOCAL = 5,
	NL80211_ATTR_MAC_LOCAL = 6,
	NL80211_ATTR_STA_INFO_LOCAL = 21,
	NL80211_ATTR_WIPHY_FREQ_LOCAL = 38,
	NL80211_ATTR_BSS_LOCAL = 47,
	NL80211_ATTR_SSID_LOCAL = 52,
};

enum {
	NL80211_STA_INFO_INACTIVE_TIME_LOCAL = 1,
	NL80211_STA_INFO_RX_BYTES_LOCAL = 2,
	NL80211_STA_INFO_TX_BYTES_LOCAL = 3,
	NL80211_STA_INFO_SIGNAL_LOCAL = 7,
	NL80211_STA_INFO_TX_BITRATE_LOCAL = 8,
	NL80211_STA_INFO_RX_PACKETS_LOCAL = 9,
	NL80211_STA_INFO_TX_PACKETS_LOCAL = 10,
	NL80211_STA_INFO_TX_RETRIES_LOCAL = 11,
	NL80211_STA_INFO_TX_FAILED_LOCAL = 12,
	NL80211_STA_INFO_RX_BITRATE_LOCAL = 14,
	NL80211_STA_INFO_CONNECTED_TIME_LOCAL = 16,
	NL80211_STA_INFO_BEACON_LOSS_LOCAL = 18,
	NL80211_STA_INFO_RX_BYTES64_LOCAL = 23,
	NL80211_STA_INFO_TX_BYTES64_LOCAL = 24,
};

enum {
	NL80211_RATE_INFO_BITRATE_LOCAL = 1,
	NL80211_RATE_INFO_BITRATE32_LOCAL = 5,
};

enum {
	NL80211_BSS_BSSID_LOCAL = 1,
	NL80211_BSS_INFORMATION_ELEMENTS_LOCAL = 6,
	NL80211_BSS_STATUS_LOCAL = 9,
};

enum {
	NL80211_BSS_STATUS_AUTHENTICATED_LOCAL = 0,
	NL80211_BSS_STATUS_ASSOCIATED_LOCAL = 1,
	NL80211_BSS_STATUS_IBSS_JOINED_LOCAL = 2,
};

#define WIFI_NL_BUF 32768
#define WIFI_MAX_IFACE 64
#define WIFI_MAX_STATION 256

typedef struct wifi_iface {
	char ifname[IFNAMSIZ];
	uint32_t ifindex;
	uint32_t freq_mhz;
} wifi_iface;

static const char *nla_data_c(const struct nlattr *a)
{
	return (const char *)a + NLA_HDRLEN;
}

static int nla_payload_len(const struct nlattr *a)
{
	return (int)a->nla_len - (int)NLA_HDRLEN;
}

static int nla_type_id(const struct nlattr *a)
{
	return a->nla_type & NLA_TYPE_MASK;
}

static struct nlattr *nla_put(char **pp, size_t *remain, uint16_t type, const void *data, size_t len)
{
	size_t total = NLA_ALIGN(NLA_HDRLEN + len);
	struct nlattr *nla;

	if (!pp || !remain || *remain < total)
		return NULL;
	nla = (struct nlattr *)*pp;
	nla->nla_type = type;
	nla->nla_len = (uint16_t)(NLA_HDRLEN + len);
	if (len) {
		if (data)
			memcpy((char *)nla + NLA_HDRLEN, data, len);
		else
			memset((char *)nla + NLA_HDRLEN, 0, len);
	}
	*pp += total;
	*remain -= total;
	return nla;
}

static void wifi_mac_str(const unsigned char m[6], char out[18])
{
	snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
		m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void wifi_sanitize_ssid(char *s)
{
	for (; s && *s; ++s) {
		if ((unsigned char)*s < 32 || *s == '"' || *s == '\\')
			*s = '_';
	}
}

static const char *wifi_bss_mode(uint32_t status)
{
	switch (status) {
	case NL80211_BSS_STATUS_AUTHENTICATED_LOCAL:
	case NL80211_BSS_STATUS_ASSOCIATED_LOCAL:
		return "client";
	case NL80211_BSS_STATUS_IBSS_JOINED_LOCAL:
		return "ad-hoc";
	default:
		return "unknown";
	}
}

static uint64_t wifi_bitrate_bps(uint32_t units_100k)
{
	return (uint64_t)units_100k * 100000ULL;
}

static void wifi_emit_interface(const char *ifname, uint32_t freq_mhz)
{
	uint64_t hz;
	char *iface;

	if (!ifname || !ifname[0] || !freq_mhz)
		return;
	iface = (char *)ifname;
	hz = (uint64_t)freq_mhz * 1000000ULL;
	metric_add_labels("wifi_interface_frequency_hertz", &hz, DATATYPE_UINT,
		ac->system_carg, "ifname", iface);
}

static void wifi_emit_bss(const char *ifname, const char *bssid, const char *ssid, const char *mode)
{
	int64_t one = 1;
	char *iface, *b, *s, *m;
	char ssidbuf[128];

	if (!ifname || !ifname[0] || !bssid || !bssid[0])
		return;
	strlcpy(ssidbuf, ssid ? ssid : "", sizeof(ssidbuf));
	wifi_sanitize_ssid(ssidbuf);
	iface = (char *)ifname;
	b = (char *)bssid;
	s = ssidbuf;
	m = (char *)(mode ? mode : "unknown");
	metric_add_labels4("wifi_station_info", &one, DATATYPE_INT, ac->system_carg,
		"ifname", iface, "bssid", b, "ssid", s, "mode", m);
}

static void wifi_emit_station(const char *ifname, const char *mac,
	int64_t signal_dbm, uint64_t rx_bytes, uint64_t tx_bytes,
	uint64_t rx_packets, uint64_t tx_packets, uint64_t tx_retries,
	uint64_t tx_failed, uint64_t beacon_loss, uint64_t connected_s,
	uint64_t inactive_ms, uint64_t rx_bitrate_100k, uint64_t tx_bitrate_100k)
{
	char *iface, *m;
	uint64_t rx_bps, tx_bps;
	double inactive_s;

	if (!ifname || !ifname[0] || !mac || !mac[0])
		return;
	iface = (char *)ifname;
	m = (char *)mac;
	inactive_s = (double)inactive_ms / 1000.0;
	rx_bps = wifi_bitrate_bps((uint32_t)rx_bitrate_100k);
	tx_bps = wifi_bitrate_bps((uint32_t)tx_bitrate_100k);

	metric_add_labels2("wifi_station_signal_dbm", &signal_dbm, DATATYPE_INT,
		ac->system_carg, "ifname", iface, "mac", m);
	metric_add_labels2("wifi_station_receive_bytes_total", &rx_bytes, DATATYPE_UINT,
		ac->system_carg, "ifname", iface, "mac", m);
	metric_add_labels2("wifi_station_transmit_bytes_total", &tx_bytes, DATATYPE_UINT,
		ac->system_carg, "ifname", iface, "mac", m);
	metric_add_labels2("wifi_station_receive_packets_total", &rx_packets, DATATYPE_UINT,
		ac->system_carg, "ifname", iface, "mac", m);
	metric_add_labels2("wifi_station_transmit_packets_total", &tx_packets, DATATYPE_UINT,
		ac->system_carg, "ifname", iface, "mac", m);
	metric_add_labels2("wifi_station_transmit_retries_total", &tx_retries, DATATYPE_UINT,
		ac->system_carg, "ifname", iface, "mac", m);
	metric_add_labels2("wifi_station_transmit_failed_total", &tx_failed, DATATYPE_UINT,
		ac->system_carg, "ifname", iface, "mac", m);
	metric_add_labels2("wifi_station_beacon_loss_total", &beacon_loss, DATATYPE_UINT,
		ac->system_carg, "ifname", iface, "mac", m);
	metric_add_labels2("wifi_station_connected_seconds_total", &connected_s, DATATYPE_UINT,
		ac->system_carg, "ifname", iface, "mac", m);
	metric_add_labels2("wifi_station_inactive_seconds", &inactive_s, DATATYPE_DOUBLE,
		ac->system_carg, "ifname", iface, "mac", m);
	metric_add_labels2("wifi_station_receive_bits_per_second", &rx_bps, DATATYPE_UINT,
		ac->system_carg, "ifname", iface, "mac", m);
	metric_add_labels2("wifi_station_transmit_bits_per_second", &tx_bps, DATATYPE_UINT,
		ac->system_carg, "ifname", iface, "mac", m);
}

void wifi_parse_dump(const char *buf, size_t size)
{
	const char *p, *end;

	if (!buf || !size)
		return;
	p = buf;
	end = buf + size;
	while (p < end) {
		const char *nl = memchr(p, '\n', (size_t)(end - p));
		size_t linelen = nl ? (size_t)(nl - p) : (size_t)(end - p);
		char line[1024];
		char *cols[16];
		int ncol = 0;
		char *save;

		if (linelen >= sizeof(line))
			linelen = sizeof(line) - 1;
		memcpy(line, p, linelen);
		line[linelen] = '\0';
		p = nl ? nl + 1 : end;
		if (!line[0] || line[0] == '#' || line[0] == '\r')
			continue;

		save = line;
		while (ncol < 16) {
			cols[ncol++] = save;
			save = strchr(save, '\t');
			if (!save)
				break;
			*save++ = '\0';
		}
		if (ncol < 2)
			continue;

		if (!strcmp(cols[0], "interface") && ncol >= 4) {
			uint32_t freq = (uint32_t)strtoul(cols[3], NULL, 10);
			wifi_emit_interface(cols[1], freq);
		} else if (!strcmp(cols[0], "bss") && ncol >= 5) {
			wifi_emit_bss(cols[1], cols[2], cols[3], cols[4]);
		} else if (!strcmp(cols[0], "station") && ncol >= 15) {
			wifi_emit_station(cols[1], cols[2],
				atoll(cols[3]),
				strtoull(cols[4], NULL, 10),
				strtoull(cols[5], NULL, 10),
				strtoull(cols[6], NULL, 10),
				strtoull(cols[7], NULL, 10),
				strtoull(cols[8], NULL, 10),
				strtoull(cols[9], NULL, 10),
				strtoull(cols[10], NULL, 10),
				strtoull(cols[11], NULL, 10),
				strtoull(cols[12], NULL, 10),
				strtoull(cols[13], NULL, 10),
				strtoull(cols[14], NULL, 10));
		}
	}
}

static uint32_t wifi_nla_u32(const struct nlattr *a)
{
	uint32_t v = 0;

	if (nla_payload_len(a) >= (int)sizeof(v))
		memcpy(&v, nla_data_c(a), sizeof(v));
	return v;
}

static uint16_t wifi_nla_u16(const struct nlattr *a)
{
	uint16_t v = 0;

	if (nla_payload_len(a) >= (int)sizeof(v))
		memcpy(&v, nla_data_c(a), sizeof(v));
	return v;
}

static uint64_t wifi_nla_u64(const struct nlattr *a)
{
	uint64_t v = 0;

	if (nla_payload_len(a) >= (int)sizeof(v))
		memcpy(&v, nla_data_c(a), sizeof(v));
	return v;
}

static void wifi_copy_nla_str(const struct nlattr *a, char *dst, size_t dstsz)
{
	int n;

	if (!dst || !dstsz)
		return;
	dst[0] = '\0';
	n = nla_payload_len(a);
	if (n <= 0)
		return;
	if ((size_t)n >= dstsz)
		n = (int)dstsz - 1;
	memcpy(dst, nla_data_c(a), (size_t)n);
	dst[n] = '\0';
	dst[strcspn(dst, "\0")] = '\0';
}

static uint32_t wifi_parse_rate_info(const struct nlattr *rate, int rlen)
{
	char *p = (char *)rate;
	int len = rlen;
	uint32_t bps100k = 0;

	while (len >= (int)NLA_HDRLEN) {
		struct nlattr *a = (struct nlattr *)p;
		int t;

		if (a->nla_len < NLA_HDRLEN || a->nla_len > len)
			break;
		t = nla_type_id(a);
		if (t == NL80211_RATE_INFO_BITRATE32_LOCAL && nla_payload_len(a) >= 4)
			bps100k = wifi_nla_u32(a);
		else if (t == NL80211_RATE_INFO_BITRATE_LOCAL && nla_payload_len(a) >= 2 && !bps100k)
			bps100k = wifi_nla_u16(a);
		p += NLA_ALIGN(a->nla_len);
		len -= NLA_ALIGN(a->nla_len);
	}
	return bps100k;
}

static void wifi_ssid_from_ies(const char *ie, int ielen, char *ssid, size_t ssidsz)
{
	int off = 0;

	if (!ssid || !ssidsz)
		return;
	ssid[0] = '\0';
	if (!ie || ielen < 2)
		return;
	while (off + 2 <= ielen) {
		unsigned char id = (unsigned char)ie[off];
		unsigned char elen = (unsigned char)ie[off + 1];
		off += 2;
		if (off + elen > ielen)
			break;
		if (id == 0) {
			size_t n = elen;
			if (n >= ssidsz)
				n = ssidsz - 1;
			memcpy(ssid, ie + off, n);
			ssid[n] = '\0';
			wifi_sanitize_ssid(ssid);
			return;
		}
		off += elen;
	}
}

static int wifi_genl_family(int fd)
{
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char payload[256];
	} req;
	char buf[4096];
	ssize_t n;
	struct nlmsghdr *nh;
	struct genlmsghdr *gh;
	char *attr;
	int alen;
	int family = -1;
	struct nlattr *na;
	size_t namelen;

	memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	req.n.nlmsg_type = GENL_ID_CTRL;
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.n.nlmsg_seq = 1;
	req.g.cmd = CTRL_CMD_GETFAMILY;
	req.g.version = 1;

	na = (struct nlattr *)((char *)&req + NLMSG_ALIGN(req.n.nlmsg_len));
	na->nla_type = CTRL_ATTR_FAMILY_NAME;
	namelen = strlen(NL80211_GENL_NAME) + 1;
	na->nla_len = (uint16_t)(NLA_HDRLEN + namelen);
	memcpy((char *)na + NLA_HDRLEN, NL80211_GENL_NAME, namelen);
	req.n.nlmsg_len += NLMSG_ALIGN(na->nla_len);

	if (send(fd, &req, req.n.nlmsg_len, 0) < 0)
		return -1;

	n = recv(fd, buf, sizeof(buf), 0);
	if (n <= 0)
		return -1;
	nh = (struct nlmsghdr *)buf;
	if (!NLMSG_OK(nh, (unsigned)n) || nh->nlmsg_type == NLMSG_ERROR)
		return -1;
	gh = NLMSG_DATA(nh);
	attr = (char *)gh + GENL_HDRLEN;
	alen = (int)(NLMSG_PAYLOAD(nh, 0) - GENL_HDRLEN);
	while (alen >= (int)NLA_HDRLEN) {
		struct nlattr *a = (struct nlattr *)attr;
		if (a->nla_len < NLA_HDRLEN || a->nla_len > alen)
			break;
		if (nla_type_id(a) == CTRL_ATTR_FAMILY_ID && nla_payload_len(a) >= (int)sizeof(uint16_t))
			family = *(uint16_t *)nla_data_c(a);
		attr += NLA_ALIGN(a->nla_len);
		alen -= NLA_ALIGN(a->nla_len);
	}
	return family;
}

static int wifi_nl_dump(int fd, int family, uint8_t cmd, uint32_t ifindex,
	void (*on_msg)(struct nlmsghdr *nh, void *arg), void *arg)
{
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char payload[64];
	} req;
	char *pp;
	size_t remain;
	char buf[WIFI_NL_BUF];
	uint32_t seq = (uint32_t)cmd + 10;

	memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	req.n.nlmsg_type = (uint16_t)family;
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	req.n.nlmsg_seq = seq;
	req.g.cmd = cmd;
	req.g.version = NL80211_GENL_VERSION;
	if (ifindex) {
		pp = req.payload;
		remain = sizeof(req.payload);
		if (!nla_put(&pp, &remain, NL80211_ATTR_IFINDEX_LOCAL, &ifindex, sizeof(ifindex)))
			return -1;
		req.n.nlmsg_len += (int)(pp - req.payload);
	}
	if (send(fd, &req, req.n.nlmsg_len, 0) < 0)
		return -1;

	for (;;) {
		ssize_t n = recv(fd, buf, sizeof(buf), 0);
		struct nlmsghdr *nh;
		int len;
		int got_done = 0;

		if (n <= 0)
			return -1;
		nh = (struct nlmsghdr *)buf;
		len = (int)n;
		for (; NLMSG_OK(nh, (unsigned)len); nh = NLMSG_NEXT(nh, len)) {
			if (nh->nlmsg_type == NLMSG_DONE) {
				got_done = 1;
				break;
			}
			if (nh->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *err = NLMSG_DATA(nh);
				if (err && err->error)
					return -1;
				got_done = 1;
				break;
			}
			if (on_msg)
				on_msg(nh, arg);
		}
		if (got_done)
			break;
	}
	return 0;
}

static void wifi_parse_interface_msg(struct nlmsghdr *nh, void *arg)
{
	wifi_iface *list = arg;
	struct genlmsghdr *gh = NLMSG_DATA(nh);
	char *attr = (char *)gh + GENL_HDRLEN;
	int alen = (int)(NLMSG_PAYLOAD(nh, 0) - GENL_HDRLEN);
	wifi_iface cur;
	int i;

	memset(&cur, 0, sizeof(cur));
	while (alen >= (int)NLA_HDRLEN) {
		struct nlattr *a = (struct nlattr *)attr;
		int t;

		if (a->nla_len < NLA_HDRLEN || a->nla_len > alen)
			break;
		t = nla_type_id(a);
		if (t == NL80211_ATTR_IFNAME_LOCAL)
			wifi_copy_nla_str(a, cur.ifname, sizeof(cur.ifname));
		else if (t == NL80211_ATTR_IFINDEX_LOCAL)
			cur.ifindex = wifi_nla_u32(a);
		else if (t == NL80211_ATTR_WIPHY_FREQ_LOCAL)
			cur.freq_mhz = wifi_nla_u32(a);
		attr += NLA_ALIGN(a->nla_len);
		alen -= NLA_ALIGN(a->nla_len);
	}
	if (!cur.ifname[0] || !cur.ifindex)
		return;
	for (i = 0; i < WIFI_MAX_IFACE; ++i) {
		if (!list[i].ifindex) {
			list[i] = cur;
			wifi_emit_interface(cur.ifname, cur.freq_mhz);
			return;
		}
		if (list[i].ifindex == cur.ifindex)
			return;
	}
}

static void wifi_parse_sta_info(const char *ifname, const char *mac, struct nlattr *info, int ilen)
{
	char *p = (char *)info;
	int len = ilen;
	int64_t signal = 0;
	uint64_t rx_bytes = 0, tx_bytes = 0, rx_packets = 0, tx_packets = 0;
	uint64_t tx_retries = 0, tx_failed = 0, beacon_loss = 0, connected_s = 0;
	uint64_t inactive_ms = 0, rx_br = 0, tx_br = 0;

	while (len >= (int)NLA_HDRLEN) {
		struct nlattr *a = (struct nlattr *)p;
		int t;

		if (a->nla_len < NLA_HDRLEN || a->nla_len > len)
			break;
		t = nla_type_id(a);
		switch (t) {
		case NL80211_STA_INFO_INACTIVE_TIME_LOCAL:
			inactive_ms = wifi_nla_u32(a);
			break;
		case NL80211_STA_INFO_RX_BYTES_LOCAL:
			if (!rx_bytes)
				rx_bytes = wifi_nla_u32(a);
			break;
		case NL80211_STA_INFO_TX_BYTES_LOCAL:
			if (!tx_bytes)
				tx_bytes = wifi_nla_u32(a);
			break;
		case NL80211_STA_INFO_RX_BYTES64_LOCAL:
			rx_bytes = wifi_nla_u64(a);
			break;
		case NL80211_STA_INFO_TX_BYTES64_LOCAL:
			tx_bytes = wifi_nla_u64(a);
			break;
		case NL80211_STA_INFO_SIGNAL_LOCAL:
			if (nla_payload_len(a) >= 1)
				signal = (int64_t)(int8_t)(unsigned char)nla_data_c(a)[0];
			break;
		case NL80211_STA_INFO_TX_BITRATE_LOCAL:
			tx_br = wifi_parse_rate_info((struct nlattr *)nla_data_c(a), nla_payload_len(a));
			break;
		case NL80211_STA_INFO_RX_BITRATE_LOCAL:
			rx_br = wifi_parse_rate_info((struct nlattr *)nla_data_c(a), nla_payload_len(a));
			break;
		case NL80211_STA_INFO_RX_PACKETS_LOCAL:
			rx_packets = wifi_nla_u32(a);
			break;
		case NL80211_STA_INFO_TX_PACKETS_LOCAL:
			tx_packets = wifi_nla_u32(a);
			break;
		case NL80211_STA_INFO_TX_RETRIES_LOCAL:
			tx_retries = wifi_nla_u32(a);
			break;
		case NL80211_STA_INFO_TX_FAILED_LOCAL:
			tx_failed = wifi_nla_u32(a);
			break;
		case NL80211_STA_INFO_CONNECTED_TIME_LOCAL:
			connected_s = wifi_nla_u32(a);
			break;
		case NL80211_STA_INFO_BEACON_LOSS_LOCAL:
			beacon_loss = wifi_nla_u32(a);
			break;
		default:
			break;
		}
		p += NLA_ALIGN(a->nla_len);
		len -= NLA_ALIGN(a->nla_len);
	}
	wifi_emit_station(ifname, mac, signal, rx_bytes, tx_bytes, rx_packets, tx_packets,
		tx_retries, tx_failed, beacon_loss, connected_s, inactive_ms, rx_br, tx_br);
}

typedef struct wifi_sta_ctx {
	const char *ifname;
	uint32_t count;
} wifi_sta_ctx;

static void wifi_parse_station_msg(struct nlmsghdr *nh, void *arg)
{
	wifi_sta_ctx *ctx = arg;
	struct genlmsghdr *gh = NLMSG_DATA(nh);
	char *attr = (char *)gh + GENL_HDRLEN;
	int alen = (int)(NLMSG_PAYLOAD(nh, 0) - GENL_HDRLEN);
	char mac[18] = "";
	struct nlattr *sta = NULL;
	int sta_len = 0;

	if (!ctx || ctx->count >= WIFI_MAX_STATION)
		return;
	while (alen >= (int)NLA_HDRLEN) {
		struct nlattr *a = (struct nlattr *)attr;
		int t;

		if (a->nla_len < NLA_HDRLEN || a->nla_len > alen)
			break;
		t = nla_type_id(a);
		if (t == NL80211_ATTR_MAC_LOCAL && nla_payload_len(a) >= 6)
			wifi_mac_str((const unsigned char *)nla_data_c(a), mac);
		else if (t == NL80211_ATTR_STA_INFO_LOCAL) {
			sta = (struct nlattr *)nla_data_c(a);
			sta_len = nla_payload_len(a);
		}
		attr += NLA_ALIGN(a->nla_len);
		alen -= NLA_ALIGN(a->nla_len);
	}
	if (!mac[0])
		return;
	++ctx->count;
	if (sta)
		wifi_parse_sta_info(ctx->ifname, mac, sta, sta_len);
	else
		wifi_emit_station(ctx->ifname, mac, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

static void wifi_parse_scan_msg(struct nlmsghdr *nh, void *arg)
{
	const char *ifname = arg;
	struct genlmsghdr *gh = NLMSG_DATA(nh);
	char *attr = (char *)gh + GENL_HDRLEN;
	int alen = (int)(NLMSG_PAYLOAD(nh, 0) - GENL_HDRLEN);
	char ssid[128] = "";
	char bssid[18] = "";
	uint32_t status = 0;
	int have_status = 0;
	char *bss = NULL;
	int bss_len = 0;

	while (alen >= (int)NLA_HDRLEN) {
		struct nlattr *a = (struct nlattr *)attr;
		int t;

		if (a->nla_len < NLA_HDRLEN || a->nla_len > alen)
			break;
		t = nla_type_id(a);
		if (t == NL80211_ATTR_SSID_LOCAL)
			wifi_copy_nla_str(a, ssid, sizeof(ssid));
		else if (t == NL80211_ATTR_BSS_LOCAL) {
			bss = (char *)nla_data_c(a);
			bss_len = nla_payload_len(a);
		}
		attr += NLA_ALIGN(a->nla_len);
		alen -= NLA_ALIGN(a->nla_len);
	}
	if (!bss)
		return;
	while (bss_len >= (int)NLA_HDRLEN) {
		struct nlattr *a = (struct nlattr *)bss;
		int t;

		if (a->nla_len < NLA_HDRLEN || a->nla_len > bss_len)
			break;
		t = nla_type_id(a);
		if (t == NL80211_BSS_BSSID_LOCAL && nla_payload_len(a) >= 6)
			wifi_mac_str((const unsigned char *)nla_data_c(a), bssid);
		else if (t == NL80211_BSS_STATUS_LOCAL) {
			status = wifi_nla_u32(a);
			have_status = 1;
		} else if (t == NL80211_BSS_INFORMATION_ELEMENTS_LOCAL && !ssid[0])
			wifi_ssid_from_ies(nla_data_c(a), nla_payload_len(a), ssid, sizeof(ssid));
		bss += NLA_ALIGN(a->nla_len);
		bss_len -= NLA_ALIGN(a->nla_len);
	}
	if (!have_status || !bssid[0])
		return;
	wifi_sanitize_ssid(ssid);
	wifi_emit_bss(ifname, bssid, ssid, wifi_bss_mode(status));
}

static void wifi_netlink_scrape(void)
{
	int fd;
	int family;
	wifi_iface ifaces[WIFI_MAX_IFACE];
	int i;

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	if (fd < 0) {
		if (errno != EPERM && errno != EACCES && errno != EPROTONOSUPPORT)
			carglog(ac->system_carg, L_DEBUG, "wifi netlink socket: %s\n", strerror(errno));
		return;
	}
	family = wifi_genl_family(fd);
	if (family < 0) {
		close(fd);
		return;
	}

	memset(ifaces, 0, sizeof(ifaces));
	wifi_nl_dump(fd, family, NL80211_CMD_GET_INTERFACE_LOCAL, 0, wifi_parse_interface_msg, ifaces);

	for (i = 0; i < WIFI_MAX_IFACE && ifaces[i].ifindex; ++i) {
		wifi_sta_ctx ctx;

		ctx.ifname = ifaces[i].ifname;
		ctx.count = 0;
		wifi_nl_dump(fd, family, NL80211_CMD_GET_STATION_LOCAL, ifaces[i].ifindex,
			wifi_parse_station_msg, &ctx);
		wifi_nl_dump(fd, family, NL80211_CMD_GET_SCAN_LOCAL, ifaces[i].ifindex,
			wifi_parse_scan_msg, (void *)ifaces[i].ifname);
	}
	close(fd);
}

void get_wifi_stats(void)
{
	char dump_path[512];
	FILE *dfd;

	if (!ac || !ac->system_procfs)
		return;
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: network: wifi nl80211\n");

	snprintf(dump_path, sizeof(dump_path), "%s/net/nl80211_dump", ac->system_procfs);
	dfd = fopen(dump_path, "r");
	if (dfd) {
		char body[8192];
		size_t n = fread(body, 1, sizeof(body) - 1, dfd);
		fclose(dfd);
		body[n] = '\0';
		wifi_parse_dump(body, n);
		return;
	}
	wifi_netlink_scrape();
}

#endif
