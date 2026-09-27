#ifdef __linux__

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/pkt_sched.h>
#include "main.h"
#include "common/logs.h"
#include "system/common.h"
#include "system/linux/qdisc.h"

extern aconf *ac;

#define QDISC_NL_BUF 32768

#ifndef NLA_TYPE_MASK
#define NLA_TYPE_MASK 0x3fff
#endif
#ifndef TCA_STATS2
#define TCA_STATS2 7
#endif
#ifndef TCA_STATS_BASIC
#define TCA_STATS_BASIC 1
#endif
#ifndef TCA_STATS_QUEUE
#define TCA_STATS_QUEUE 3
#endif
#ifndef TCA_STATS_PKT64
#define TCA_STATS_PKT64 8
#endif
#ifndef TCA_RTA
#define TCA_RTA(r) ((struct rtattr *)(((char *)(r)) + NLMSG_ALIGN(sizeof(struct tcmsg))))
#endif

static void qdisc_emit(const char *device, const char *kind,
	uint64_t bytes, uint64_t packets,
	uint32_t drops, uint32_t requeues, uint32_t overlimits,
	uint32_t qlen, uint32_t backlog)
{
	uint64_t v;

	v = bytes;
	metric_add_labels2("qdisc_bytes_total", &v, DATATYPE_UINT, ac->system_carg,
		"device", (char *)device, "kind", (char *)kind);
	v = packets;
	metric_add_labels2("qdisc_packets_total", &v, DATATYPE_UINT, ac->system_carg,
		"device", (char *)device, "kind", (char *)kind);
	v = drops;
	metric_add_labels2("qdisc_drops_total", &v, DATATYPE_UINT, ac->system_carg,
		"device", (char *)device, "kind", (char *)kind);
	v = requeues;
	metric_add_labels2("qdisc_requeues_total", &v, DATATYPE_UINT, ac->system_carg,
		"device", (char *)device, "kind", (char *)kind);
	v = overlimits;
	metric_add_labels2("qdisc_overlimits_total", &v, DATATYPE_UINT, ac->system_carg,
		"device", (char *)device, "kind", (char *)kind);
	v = qlen;
	metric_add_labels2("qdisc_queue_length", &v, DATATYPE_UINT, ac->system_carg,
		"device", (char *)device, "kind", (char *)kind);
	v = backlog;
	metric_add_labels2("qdisc_backlog_bytes", &v, DATATYPE_UINT, ac->system_carg,
		"device", (char *)device, "kind", (char *)kind);
}

static void parse_stats2(struct rtattr *stats2,
	uint64_t *bytes, uint64_t *packets,
	uint32_t *drops, uint32_t *requeues, uint32_t *overlimits,
	uint32_t *qlen, uint32_t *backlog)
{
	int rem = RTA_PAYLOAD(stats2);
	struct rtattr *attr;
	uint32_t basic_packets = 0;
	uint64_t packets64 = 0;
	uint8_t has_pkt64 = 0;
	uint16_t prev = 0;

	for (attr = RTA_DATA(stats2); RTA_OK(attr, rem); attr = RTA_NEXT(attr, rem)) {
		uint16_t type = attr->rta_type & NLA_TYPE_MASK;
		if (type == TCA_STATS_BASIC && RTA_PAYLOAD(attr) >= 12) {
			const unsigned char *p = RTA_DATA(attr);
			memcpy(bytes, p, 8);
			memcpy(&basic_packets, p + 8, 4);
		} else if (type == TCA_STATS_QUEUE && RTA_PAYLOAD(attr) >= 20) {
			const unsigned char *p = RTA_DATA(attr);
			memcpy(qlen, p, 4);
			memcpy(backlog, p + 4, 4);
			memcpy(drops, p + 8, 4);
			memcpy(requeues, p + 12, 4);
			memcpy(overlimits, p + 16, 4);
		} else if (type == TCA_STATS_PKT64 && RTA_PAYLOAD(attr) >= 8 && prev == TCA_STATS_BASIC) {
			memcpy(&packets64, RTA_DATA(attr), sizeof(packets64));
			has_pkt64 = 1;
		}
		prev = type;
	}

	*packets = has_pkt64 ? packets64 : (uint64_t)basic_packets;
}

static void parse_legacy_stats(struct rtattr *attr,
	uint64_t *bytes, uint64_t *packets,
	uint32_t *drops, uint32_t *overlimits,
	uint32_t *qlen, uint32_t *backlog)
{
	/* struct tc_stats layout: bytes(8) packets(4) drops(4) overlimits(4)
	 * bps(4) pps(4) qlen(4) backlog(4) — 36 bytes minimum */
	const unsigned char *p = RTA_DATA(attr);
	unsigned int plen = RTA_PAYLOAD(attr);
	if (plen < 36)
		return;
	memcpy(bytes, p, 8);
	{
		uint32_t pkts;
		memcpy(&pkts, p + 8, 4);
		*packets = pkts;
	}
	memcpy(drops, p + 12, 4);
	memcpy(overlimits, p + 16, 4);
	memcpy(qlen, p + 28, 4);
	memcpy(backlog, p + 32, 4);
}

int qdisc_parse_attrs_emit(const char *device, uint32_t parent, const void *attrs, size_t attrlen)
{
	char kind[64] = "";
	uint64_t bytes = 0, packets = 0;
	uint32_t drops = 0, requeues = 0, overlimits = 0, qlen = 0, backlog = 0;
	uint8_t has_stats2 = 0;
	int rem;
	struct rtattr *attr;

	if (!device || !device[0] || parent != TC_H_ROOT)
		return 0;
	if (system_iface_is_veth(device))
		return 0;
	if (!attrs || attrlen < sizeof(struct rtattr))
		return 0;

	rem = (int)attrlen;
	for (attr = (struct rtattr *)attrs; RTA_OK(attr, rem); attr = RTA_NEXT(attr, rem)) {
		if (attr->rta_type == TCA_KIND) {
			unsigned int plen = RTA_PAYLOAD(attr);
			if (!plen)
				continue;
			if (plen >= sizeof(kind))
				plen = sizeof(kind) - 1;
			memcpy(kind, RTA_DATA(attr), plen);
			kind[plen] = '\0';
		} else if (attr->rta_type == TCA_STATS2) {
			parse_stats2(attr, &bytes, &packets, &drops, &requeues, &overlimits, &qlen, &backlog);
			has_stats2 = 1;
		} else if (!has_stats2 && attr->rta_type == TCA_STATS) {
			parse_legacy_stats(attr, &bytes, &packets, &drops, &overlimits, &qlen, &backlog);
		}
	}

	if (!kind[0] || !strcmp(kind, "noqueue"))
		return 0;

	qdisc_emit(device, kind, bytes, packets, drops, requeues, overlimits, qlen, backlog);
	return 1;
}

static int qdisc_recv_dump(int fd)
{
	char buf[QDISC_NL_BUF];

	for (;;) {
		ssize_t len = recv(fd, buf, sizeof(buf), 0);
		if (len < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (len == 0)
			break;

		struct nlmsghdr *nh;
		int remaining = (int)len;
		for (nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, remaining); nh = NLMSG_NEXT(nh, remaining)) {
			if (nh->nlmsg_type == NLMSG_DONE)
				return 0;
			if (nh->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *err = NLMSG_DATA(nh);
				if (err->error)
					return -1;
				continue;
			}
			if (nh->nlmsg_type != RTM_NEWQDISC)
				continue;

			struct tcmsg *tcm = NLMSG_DATA(nh);
			int attrlen = (int)(nh->nlmsg_len - NLMSG_LENGTH(sizeof(*tcm)));
			if (attrlen < 0)
				continue;

			char ifname[IFNAMSIZ];
			if (!if_indextoname((unsigned int)tcm->tcm_ifindex, ifname))
				continue;

			qdisc_parse_attrs_emit(ifname, tcm->tcm_parent, TCA_RTA(tcm), (size_t)attrlen);
		}
	}
	return 0;
}

void get_qdisc_stats(void)
{
	int fd;
	struct sockaddr_nl addr;
	socklen_t addr_len = sizeof(addr);

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (fd < 0)
		return;

	memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return;
	}
	if (getsockname(fd, (struct sockaddr *)&addr, &addr_len) < 0) {
		close(fd);
		return;
	}

	struct {
		struct nlmsghdr nlh;
		struct tcmsg tcm;
	} req;

	memset(&req, 0, sizeof(req));
	req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg));
	req.nlh.nlmsg_type = RTM_GETQDISC;
	req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	req.nlh.nlmsg_seq = 1;
	req.nlh.nlmsg_pid = addr.nl_pid;
	req.tcm.tcm_family = AF_UNSPEC;

	for (;;) {
		ssize_t sent = send(fd, &req, req.nlh.nlmsg_len, 0);
		if (sent < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			return;
		}
		break;
	}

	qdisc_recv_dump(fd);
	close(fd);
}

#endif
