#ifdef __linux__

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/sockios.h>
#include <linux/ethtool.h>

#ifndef NLA_TYPE_MASK
#define NLA_TYPE_MASK 0x3fff
#endif
#ifndef NLA_F_NESTED
#define NLA_F_NESTED (1 << 15)
#endif

#include "main.h"
#include "common/logs.h"
#include "system/common.h"
#include "system/linux/ethtool.h"

extern aconf *ac;

/* Minimal UAPI from linux/ethtool_netlink.h (not always present on older libc headers). */
#ifndef ETHTOOL_GENL_NAME
#define ETHTOOL_GENL_NAME "ethtool"
#endif
#ifndef ETHTOOL_GENL_VERSION
#define ETHTOOL_GENL_VERSION 1
#endif

enum {
	ETHTOOL_MSG_USER_NONE_LOCAL = 0,
	ETHTOOL_MSG_STRSET_GET_LOCAL = 1,
	ETHTOOL_MSG_STATS_GET_LOCAL = 32,
};

enum {
	ETHTOOL_A_HEADER_UNSPEC_LOCAL = 0,
	ETHTOOL_A_HEADER_DEV_INDEX_LOCAL = 1,
	ETHTOOL_A_HEADER_DEV_NAME_LOCAL = 2,
	ETHTOOL_A_HEADER_FLAGS_LOCAL = 3,
};

enum {
	ETHTOOL_A_BITSET_UNSPEC_LOCAL = 0,
	ETHTOOL_A_BITSET_NOMASK_LOCAL = 1,
	ETHTOOL_A_BITSET_SIZE_LOCAL = 2,
	ETHTOOL_A_BITSET_BITS_LOCAL = 3,
	ETHTOOL_A_BITSET_VALUE_LOCAL = 4,
	ETHTOOL_A_BITSET_MASK_LOCAL = 5,
};

enum {
	ETHTOOL_A_STATS_UNSPEC_LOCAL = 0,
	ETHTOOL_A_STATS_PAD_LOCAL = 1,
	ETHTOOL_A_STATS_HEADER_LOCAL = 2,
	ETHTOOL_A_STATS_GROUPS_LOCAL = 3,
	ETHTOOL_A_STATS_GRP_LOCAL = 4,
	ETHTOOL_A_STATS_SRC_LOCAL = 5,
};

enum {
	ETHTOOL_STATS_ETH_PHY_LOCAL = 0,
	ETHTOOL_STATS_ETH_MAC_LOCAL = 1,
	ETHTOOL_STATS_ETH_CTRL_LOCAL = 2,
	ETHTOOL_STATS_RMON_LOCAL = 3,
	ETHTOOL_STATS_CNT_LOCAL = 4,
};

enum {
	ETHTOOL_A_STATS_GRP_UNSPEC_LOCAL = 0,
	ETHTOOL_A_STATS_GRP_PAD_LOCAL = 1,
	ETHTOOL_A_STATS_GRP_ID_LOCAL = 2,
	ETHTOOL_A_STATS_GRP_SS_ID_LOCAL = 3,
	ETHTOOL_A_STATS_GRP_STAT_LOCAL = 4,
	ETHTOOL_A_STATS_GRP_HIST_RX_LOCAL = 5,
	ETHTOOL_A_STATS_GRP_HIST_TX_LOCAL = 6,
	ETHTOOL_A_STATS_GRP_HIST_BKT_LOW_LOCAL = 7,
	ETHTOOL_A_STATS_GRP_HIST_BKT_HI_LOCAL = 8,
	ETHTOOL_A_STATS_GRP_HIST_VAL_LOCAL = 9,
};

#define ETHTOOL_STAT_NOT_SET_LOCAL (~(uint64_t)0)
#define ETHTOOL_IOCTL_MAX_STATS 512
#define ETHTOOL_NL_BUF 32768

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

static struct nlattr *nla_nest_start(char **pp, size_t *remain, uint16_t type)
{
	return nla_put(pp, remain, type | NLA_F_NESTED, NULL, 0);
}

static void nla_nest_end(struct nlattr *start, char *end)
{
	if (start)
		start->nla_len = (uint16_t)(end - (char *)start);
}

static const char *ethtool_group_name(uint32_t id)
{
	switch (id) {
	case ETHTOOL_STATS_ETH_PHY_LOCAL: return "eth_phy";
	case ETHTOOL_STATS_ETH_MAC_LOCAL: return "eth_mac";
	case ETHTOOL_STATS_ETH_CTRL_LOCAL: return "eth_ctrl";
	case ETHTOOL_STATS_RMON_LOCAL: return "rmon";
	default: return "unknown";
	}
}

static const char *ethtool_std_stat_name(uint32_t group, uint32_t id)
{
	if (group == ETHTOOL_STATS_ETH_PHY_LOCAL) {
		if (id == 0) return "sym_err";
		return NULL;
	}
	if (group == ETHTOOL_STATS_ETH_MAC_LOCAL) {
		static const char *mac[] = {
			"tx_packets", "tx_single_collisions", "tx_multi_collisions",
			"rx_packets", "rx_fcs_errors", "rx_align_errors",
			"tx_bytes", "tx_deferred", "tx_late_collisions",
			"tx_excessive_collisions", "tx_internal_errors", "carrier_sense_errors",
			"rx_bytes", "rx_internal_errors",
			"tx_multicast", "tx_broadcast", "tx_excessive_deferred",
			"rx_multicast", "rx_broadcast", "rx_in_range_length_errors",
			"rx_out_of_range_length", "rx_too_long_errors",
		};
		if (id < sizeof(mac) / sizeof(mac[0]))
			return mac[id];
		return NULL;
	}
	if (group == ETHTOOL_STATS_ETH_CTRL_LOCAL) {
		static const char *ctrl[] = { "tx_mac_control", "rx_mac_control", "rx_unsupported_opcodes" };
		if (id < sizeof(ctrl) / sizeof(ctrl[0]))
			return ctrl[id];
		return NULL;
	}
	if (group == ETHTOOL_STATS_RMON_LOCAL) {
		static const char *rmon[] = { "undersize", "oversize", "fragments", "jabbers" };
		if (id < sizeof(rmon) / sizeof(rmon[0]))
			return rmon[id];
		return NULL;
	}
	return NULL;
}

static void ethtool_sanitize_stat(const char *in, char *out, size_t outsz)
{
	size_t i, o = 0;

	if (!outsz)
		return;
	if (!in) {
		out[0] = 0;
		return;
	}
	for (i = 0; in[i] && o + 1 < outsz; i++) {
		unsigned char c = (unsigned char)in[i];
		if (isalnum(c) || c == '_' || c == '-' || c == '.')
			out[o++] = (char)c;
		else if (o && out[o - 1] != '_')
			out[o++] = '_';
	}
	while (o && out[o - 1] == '_')
		o--;
	out[o] = 0;
	if (!out[0])
		snprintf(out, outsz, "stat");
}

static void ethtool_emit_std(const char *ifname, const char *group, const char *stat, uint64_t val)
{
	char *iface = (char *)ifname;
	char *grp = (char *)group;
	char *st = (char *)stat;

	if (!ifname || !group || !stat || val == ETHTOOL_STAT_NOT_SET_LOCAL)
		return;
	metric_add_labels3("ethtool_std_stat", &val, DATATYPE_UINT, ac->system_carg,
		"ifname", iface, "group", grp, "stat", st);
}

static void ethtool_emit_driver(const char *ifname, const char *stat, uint64_t val)
{
	char *iface = (char *)ifname;
	char *st = (char *)stat;

	if (!ifname || !stat || !stat[0])
		return;
	metric_add_labels2("ethtool_stat", &val, DATATYPE_UINT, ac->system_carg,
		"ifname", iface, "stat", st);
}

static void ethtool_emit_rmon_hist(const char *ifname, const char *direction,
	uint32_t low, uint32_t high, uint64_t val)
{
	char lowbuf[16], highbuf[16];
	char *iface = (char *)ifname;
	char *dir = (char *)direction;

	if (!ifname || !direction || val == ETHTOOL_STAT_NOT_SET_LOCAL)
		return;
	snprintf(lowbuf, sizeof(lowbuf), "%u", low);
	snprintf(highbuf, sizeof(highbuf), "%u", high);
	metric_add_labels4("ethtool_rmon_hist", &val, DATATYPE_UINT, ac->system_carg,
		"ifname", iface, "direction", dir, "bucket_low", lowbuf, "bucket_hi", highbuf);
}

static int ethtool_genl_family(int fd)
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
	namelen = strlen(ETHTOOL_GENL_NAME) + 1;
	na->nla_len = (uint16_t)(NLA_HDRLEN + namelen);
	memcpy((char *)na + NLA_HDRLEN, ETHTOOL_GENL_NAME, namelen);
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

static void ethtool_parse_header(struct nlattr *hdr, int hlen, char *ifname, size_t ifsz)
{
	char *p = (char *)hdr;
	int len = hlen;

	if (ifname && ifsz)
		ifname[0] = 0;
	while (len >= (int)NLA_HDRLEN) {
		struct nlattr *a = (struct nlattr *)p;
		if (a->nla_len < NLA_HDRLEN || a->nla_len > len)
			break;
		if (nla_type_id(a) == ETHTOOL_A_HEADER_DEV_NAME_LOCAL && nla_payload_len(a) > 0 && ifname && ifsz) {
			size_t n = (size_t)nla_payload_len(a);
			if (n >= ifsz)
				n = ifsz - 1;
			memcpy(ifname, nla_data_c(a), n);
			ifname[n] = 0;
		}
		p += NLA_ALIGN(a->nla_len);
		len -= NLA_ALIGN(a->nla_len);
	}
}

static void ethtool_parse_hist(const char *ifname, const char *direction, struct nlattr *hist, int hlen)
{
	char *p = (char *)hist;
	int len = hlen;
	uint32_t low = 0, high = 0;
	uint64_t val = ETHTOOL_STAT_NOT_SET_LOCAL;
	uint8_t have_low = 0, have_high = 0, have_val = 0;

	while (len >= (int)NLA_HDRLEN) {
		struct nlattr *a = (struct nlattr *)p;
		if (a->nla_len < NLA_HDRLEN || a->nla_len > len)
			break;
		switch (nla_type_id(a)) {
		case ETHTOOL_A_STATS_GRP_HIST_BKT_LOW_LOCAL:
			if (nla_payload_len(a) >= (int)sizeof(uint32_t)) {
				memcpy(&low, nla_data_c(a), sizeof(low));
				have_low = 1;
			}
			break;
		case ETHTOOL_A_STATS_GRP_HIST_BKT_HI_LOCAL:
			if (nla_payload_len(a) >= (int)sizeof(uint32_t)) {
				memcpy(&high, nla_data_c(a), sizeof(high));
				have_high = 1;
			}
			break;
		case ETHTOOL_A_STATS_GRP_HIST_VAL_LOCAL:
			if (nla_payload_len(a) >= (int)sizeof(uint64_t)) {
				memcpy(&val, nla_data_c(a), sizeof(val));
				have_val = 1;
			}
			break;
		default:
			break;
		}
		p += NLA_ALIGN(a->nla_len);
		len -= NLA_ALIGN(a->nla_len);
	}
	if (have_low && have_high && have_val)
		ethtool_emit_rmon_hist(ifname, direction, low, high, val);
}

static void ethtool_parse_grp_stat(const char *ifname, uint32_t group, struct nlattr *statnest, int slen)
{
	char *p = (char *)statnest;
	int len = slen;
	char idbuf[32];

	while (len >= (int)NLA_HDRLEN) {
		struct nlattr *a = (struct nlattr *)p;
		const char *name;
		uint64_t val;
		uint32_t sid;

		if (a->nla_len < NLA_HDRLEN || a->nla_len > len)
			break;
		sid = (uint32_t)nla_type_id(a);
		if (nla_payload_len(a) >= (int)sizeof(uint64_t)) {
			memcpy(&val, nla_data_c(a), sizeof(val));
			name = ethtool_std_stat_name(group, sid);
			if (!name) {
				snprintf(idbuf, sizeof(idbuf), "id_%u", sid);
				name = idbuf;
			}
			ethtool_emit_std(ifname, ethtool_group_name(group), name, val);
		}
		p += NLA_ALIGN(a->nla_len);
		len -= NLA_ALIGN(a->nla_len);
	}
}

static void ethtool_parse_grp(const char *ifname, struct nlattr *grp, int glen)
{
	char *p = (char *)grp;
	int len = glen;
	uint32_t group = UINT32_MAX;

	while (len >= (int)NLA_HDRLEN) {
		struct nlattr *a = (struct nlattr *)p;
		if (a->nla_len < NLA_HDRLEN || a->nla_len > len)
			break;
		switch (nla_type_id(a)) {
		case ETHTOOL_A_STATS_GRP_ID_LOCAL:
			if (nla_payload_len(a) >= (int)sizeof(uint32_t))
				memcpy(&group, nla_data_c(a), sizeof(group));
			break;
		case ETHTOOL_A_STATS_GRP_STAT_LOCAL:
			if (group != UINT32_MAX)
				ethtool_parse_grp_stat(ifname, group, (struct nlattr *)nla_data_c(a), nla_payload_len(a));
			break;
		case ETHTOOL_A_STATS_GRP_HIST_RX_LOCAL:
			ethtool_parse_hist(ifname, "rx", (struct nlattr *)nla_data_c(a), nla_payload_len(a));
			break;
		case ETHTOOL_A_STATS_GRP_HIST_TX_LOCAL:
			ethtool_parse_hist(ifname, "tx", (struct nlattr *)nla_data_c(a), nla_payload_len(a));
			break;
		default:
			break;
		}
		p += NLA_ALIGN(a->nla_len);
		len -= NLA_ALIGN(a->nla_len);
	}
}

static void ethtool_parse_stats_msg(struct nlmsghdr *nh)
{
	struct genlmsghdr *gh = NLMSG_DATA(nh);
	char *attr = (char *)gh + GENL_HDRLEN;
	int alen = (int)(NLMSG_PAYLOAD(nh, 0) - GENL_HDRLEN);
	char ifname[IF_NAMESIZE] = "";

	while (alen >= (int)NLA_HDRLEN) {
		struct nlattr *a = (struct nlattr *)attr;
		if (a->nla_len < NLA_HDRLEN || a->nla_len > alen)
			break;
		switch (nla_type_id(a)) {
		case ETHTOOL_A_STATS_HEADER_LOCAL:
			ethtool_parse_header((struct nlattr *)nla_data_c(a), nla_payload_len(a), ifname, sizeof(ifname));
			break;
		case ETHTOOL_A_STATS_GRP_LOCAL:
			if (ifname[0] && !system_iface_is_veth(ifname) && strcmp(ifname, "lo"))
				ethtool_parse_grp(ifname, (struct nlattr *)nla_data_c(a), nla_payload_len(a));
			break;
		default:
			break;
		}
		attr += NLA_ALIGN(a->nla_len);
		alen -= NLA_ALIGN(a->nla_len);
	}
}

static int ethtool_build_stats_req(char *buf, size_t bufsz, int family, uint32_t seq,
	const char *ifname, int dump)
{
	struct nlmsghdr *nh;
	struct genlmsghdr *gh;
	char *p;
	size_t remain;
	struct nlattr *stats_hdr;
	struct nlattr *groups;
	struct nlattr *dev;
	uint32_t bit_size;
	uint32_t bit_value;

	if (bufsz < sizeof(struct nlmsghdr) + GENL_HDRLEN)
		return -1;
	memset(buf, 0, bufsz);
	nh = (struct nlmsghdr *)buf;
	gh = (struct genlmsghdr *)(buf + sizeof(*nh));
	nh->nlmsg_type = (uint16_t)family;
	nh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | (dump ? NLM_F_DUMP : 0);
	nh->nlmsg_seq = seq;
	nh->nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	gh->cmd = ETHTOOL_MSG_STATS_GET_LOCAL;
	gh->version = ETHTOOL_GENL_VERSION;

	p = buf + NLMSG_ALIGN(nh->nlmsg_len);
	remain = bufsz - (size_t)(p - buf);

	stats_hdr = nla_nest_start(&p, &remain, ETHTOOL_A_STATS_HEADER_LOCAL);
	if (!stats_hdr)
		return -1;
	if (ifname && ifname[0]) {
		dev = nla_put(&p, &remain, ETHTOOL_A_HEADER_DEV_NAME_LOCAL, ifname, strlen(ifname) + 1);
		if (!dev)
			return -1;
	}
	nla_nest_end(stats_hdr, p);

	groups = nla_nest_start(&p, &remain, ETHTOOL_A_STATS_GROUPS_LOCAL);
	if (!groups)
		return -1;
	if (!nla_put(&p, &remain, ETHTOOL_A_BITSET_NOMASK_LOCAL, NULL, 0))
		return -1;
	bit_size = ETHTOOL_STATS_CNT_LOCAL;
	if (!nla_put(&p, &remain, ETHTOOL_A_BITSET_SIZE_LOCAL, &bit_size, sizeof(bit_size)))
		return -1;
	bit_value = (1u << ETHTOOL_STATS_ETH_PHY_LOCAL) |
		(1u << ETHTOOL_STATS_ETH_MAC_LOCAL) |
		(1u << ETHTOOL_STATS_ETH_CTRL_LOCAL) |
		(1u << ETHTOOL_STATS_RMON_LOCAL);
	if (!nla_put(&p, &remain, ETHTOOL_A_BITSET_VALUE_LOCAL, &bit_value, sizeof(bit_value)))
		return -1;
	nla_nest_end(groups, p);

	nh->nlmsg_len = (uint32_t)(p - buf);
	return (int)nh->nlmsg_len;
}

static int ethtool_genl_recv_until_done(int fd, void (*on_msg)(struct nlmsghdr *))
{
	char buf[ETHTOOL_NL_BUF];

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
				if (err->error)
					return err->error;
				got_done = 1;
				break;
			}
			if (on_msg)
				on_msg(nh);
		}
		if (got_done)
			return 0;
	}
}

static int ethtool_genl_dump(int fd, int family)
{
	char req[512];
	int len = ethtool_build_stats_req(req, sizeof(req), family, 2, NULL, 1);

	if (len < 0)
		return -1;
	if (send(fd, req, (size_t)len, 0) < 0)
		return -1;
	return ethtool_genl_recv_until_done(fd, ethtool_parse_stats_msg);
}

static int ethtool_genl_one(int fd, int family, const char *ifname, uint32_t seq)
{
	char req[512];
	int len = ethtool_build_stats_req(req, sizeof(req), family, seq, ifname, 0);

	if (len < 0)
		return -1;
	if (send(fd, req, (size_t)len, 0) < 0)
		return -1;
	return ethtool_genl_recv_until_done(fd, ethtool_parse_stats_msg);
}

static void ethtool_genl_foreach_sysfs(int fd, int family)
{
	DIR *dp;
	struct dirent *entry;
	char classpath[255];
	uint32_t seq = 10;

	snprintf(classpath, sizeof(classpath), "%s/class/net", ac->system_sysfs);
	dp = opendir(classpath);
	if (!dp)
		return;
	while ((entry = readdir(dp))) {
		if (entry->d_name[0] == '.')
			continue;
		if (system_iface_is_veth(entry->d_name) || !strcmp(entry->d_name, "lo"))
			continue;
		ethtool_genl_one(fd, family, entry->d_name, seq++);
	}
	closedir(dp);
}

static void ethtool_ioctl_iface(int fd, const char *ifname)
{
	struct ifreq ifr;
	struct ethtool_drvinfo drv;
	struct ethtool_gstrings *strings = NULL;
	struct ethtool_stats *stats = NULL;
	uint32_t n_stats;
	uint32_t i;
	size_t slen;
	size_t stlen;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, IFNAMSIZ);

	memset(&drv, 0, sizeof(drv));
	drv.cmd = ETHTOOL_GDRVINFO;
	ifr.ifr_data = (void *)&drv;
	if (ioctl(fd, SIOCETHTOOL, &ifr) < 0)
		return;
	n_stats = drv.n_stats;
	if (!n_stats)
		return;
	if (n_stats > ETHTOOL_IOCTL_MAX_STATS)
		n_stats = ETHTOOL_IOCTL_MAX_STATS;

	slen = sizeof(*strings) + (size_t)n_stats * ETH_GSTRING_LEN;
	strings = calloc(1, slen);
	if (!strings)
		return;
	strings->cmd = ETHTOOL_GSTRINGS;
	strings->string_set = ETH_SS_STATS;
	strings->len = n_stats;
	ifr.ifr_data = (void *)strings;
	if (ioctl(fd, SIOCETHTOOL, &ifr) < 0)
		goto out;

	stlen = sizeof(*stats) + (size_t)n_stats * sizeof(uint64_t);
	stats = calloc(1, stlen);
	if (!stats)
		goto out;
	stats->cmd = ETHTOOL_GSTATS;
	stats->n_stats = n_stats;
	ifr.ifr_data = (void *)stats;
	if (ioctl(fd, SIOCETHTOOL, &ifr) < 0)
		goto out;

	for (i = 0; i < n_stats; i++) {
		char raw[ETH_GSTRING_LEN + 1];
		char name[ETH_GSTRING_LEN + 1];

		memcpy(raw, strings->data + i * ETH_GSTRING_LEN, ETH_GSTRING_LEN);
		raw[ETH_GSTRING_LEN] = 0;
		ethtool_sanitize_stat(raw, name, sizeof(name));
		ethtool_emit_driver(ifname, name, stats->data[i]);
	}

out:
	free(stats);
	free(strings);
}

static void ethtool_ioctl_foreach_sysfs(void)
{
	DIR *dp;
	struct dirent *entry;
	char classpath[255];
	int fd;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return;

	snprintf(classpath, sizeof(classpath), "%s/class/net", ac->system_sysfs);
	dp = opendir(classpath);
	if (!dp) {
		close(fd);
		return;
	}
	while ((entry = readdir(dp))) {
		if (entry->d_name[0] == '.')
			continue;
		if (system_iface_is_veth(entry->d_name) || !strcmp(entry->d_name, "lo"))
			continue;
		ethtool_ioctl_iface(fd, entry->d_name);
	}
	closedir(dp);
	close(fd);
}

void get_ethtool_stats(void)
{
	int fd;
	int family;

	carglog(ac->system_carg, L_TRACE, "system scrape metrics: network: ethtool\n");

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	if (fd >= 0) {
		family = ethtool_genl_family(fd);
		if (family >= 0) {
			if (ethtool_genl_dump(fd, family) < 0)
				ethtool_genl_foreach_sysfs(fd, family);
		} else {
			carglog(ac->system_carg, L_DEBUG,
				"ethtool genetlink family unavailable (need Linux ~5.8+)\n");
		}
		close(fd);
	} else if (errno != EPERM && errno != EACCES && errno != EPROTONOSUPPORT) {
		carglog(ac->system_carg, L_DEBUG, "ethtool genetlink socket: %s\n", strerror(errno));
	}

	/* Driver-defined counters (ethtool -S / node_exporter). Genetlink STATS_GET is IEEE/RMON only. */
	ethtool_ioctl_foreach_sysfs();
}

#endif
