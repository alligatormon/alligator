#ifdef __linux__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/socket.h>
#include <time.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#ifndef NLA_TYPE_MASK
#define NLA_TYPE_MASK 0x3fff
#endif
#include "main.h"
#include "common/logs.h"
#include "system/linux/wireguard.h"

extern aconf *ac;

static void wg_emit_device(char *device, uint64_t listen_port, uint64_t peers)
{
	metric_add_labels("wireguard_device_listen_port", &listen_port, DATATYPE_UINT,
		ac->system_carg, "device", device);
	metric_add_labels("wireguard_device_peers", &peers, DATATYPE_UINT,
		ac->system_carg, "device", device);
}

static void wg_emit_peer(char *device, char *public_key,
	uint64_t rx, uint64_t tx, uint64_t handshake)
{
	metric_add_labels2("wireguard_peer_rx_bytes", &rx, DATATYPE_UINT,
		ac->system_carg, "device", device, "public_key", public_key);
	metric_add_labels2("wireguard_peer_tx_bytes", &tx, DATATYPE_UINT,
		ac->system_carg, "device", device, "public_key", public_key);
	metric_add_labels2("wireguard_peer_last_handshake_seconds", &handshake, DATATYPE_UINT,
		ac->system_carg, "device", device, "public_key", public_key);
}

void wireguard_parse_dump(const char *buf, size_t size)
{
	if (!buf || !size)
		return;

	char device[64] = "";
	uint64_t peer_count = 0;
	uint64_t listen_port = 0;
	const char *p = buf;
	const char *end = buf + size;

	while (p < end) {
		const char *nl = memchr(p, '\n', (size_t)(end - p));
		size_t linelen = nl ? (size_t)(nl - p) : (size_t)(end - p);
		char line[1024];
		if (linelen >= sizeof(line))
			linelen = sizeof(line) - 1;
		memcpy(line, p, linelen);
		line[linelen] = '\0';
		p = nl ? nl + 1 : end;
		if (!line[0] || line[0] == '\r')
			continue;

		char *cols[8];
		int ncol = 0;
		char *save = line;
		while (ncol < 8) {
			cols[ncol] = save;
			char *tab = strchr(save, '\t');
			if (!tab) {
				++ncol;
				break;
			}
			*tab = '\0';
			save = tab + 1;
			++ncol;
		}

		if (ncol == 5 && !strchr(cols[0], ':') && !strchr(cols[0], '/')) {
			if (device[0])
				wg_emit_device(device, listen_port, peer_count);
			strlcpy(device, cols[0], sizeof(device));
			listen_port = strtoull(cols[3], NULL, 10);
			peer_count = 0;
			continue;
		}
		if (ncol >= 8 && device[0]) {
			++peer_count;
			uint64_t handshake = strtoull(cols[4], NULL, 10);
			uint64_t rx = strtoull(cols[5], NULL, 10);
			uint64_t tx = strtoull(cols[6], NULL, 10);
			wg_emit_peer(device, cols[0], rx, tx, handshake);
		}
	}
	if (device[0])
		wg_emit_device(device, listen_port, peer_count);
}

#define WGDEVICE_A_IFNAME 2
#define WGDEVICE_A_LISTEN_PORT 6
#define WGDEVICE_A_PEERS 8
#define WGPEER_A_PUBLIC_KEY 1
#define WGPEER_A_LAST_HANDSHAKE_TIME 8
#define WGPEER_A_RX_BYTES 9
#define WGPEER_A_TX_BYTES 10

static void wg_base64_32(const unsigned char in[32], char out[45])
{
	static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t i, o = 0;
	for (i = 0; i < 30; i += 3) {
		uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
		out[o++] = tbl[(v >> 18) & 63];
		out[o++] = tbl[(v >> 12) & 63];
		out[o++] = tbl[(v >> 6) & 63];
		out[o++] = tbl[v & 63];
	}
	{
		uint32_t v = ((uint32_t)in[30] << 16) | ((uint32_t)in[31] << 8);
		out[o++] = tbl[(v >> 18) & 63];
		out[o++] = tbl[(v >> 12) & 63];
		out[o++] = tbl[(v >> 6) & 63];
		out[o++] = '=';
	}
	out[o] = '\0';
}

static const char *nla_data(const struct nlattr *a)
{
	return (const char *)a + NLA_HDRLEN;
}

static int nla_len(const struct nlattr *a)
{
	return (int)a->nla_len - (int)NLA_HDRLEN;
}

static void wg_parse_peer(char *device, struct nlattr *peer, int plen)
{
	char pubkey[45] = "";
	uint64_t rx = 0, tx = 0, handshake = 0;
	int len = plen;
	char *p = (char *)peer;
	while (len >= (int)NLA_HDRLEN) {
		struct nlattr *a = (struct nlattr *)p;
		if (a->nla_len < NLA_HDRLEN || a->nla_len > len)
			break;
		int t = a->nla_type & NLA_TYPE_MASK;
		if (t == WGPEER_A_PUBLIC_KEY && nla_len(a) >= 32)
			wg_base64_32((const unsigned char *)nla_data(a), pubkey);
		else if (t == WGPEER_A_RX_BYTES && nla_len(a) >= (int)sizeof(uint64_t))
			memcpy(&rx, nla_data(a), sizeof(rx));
		else if (t == WGPEER_A_TX_BYTES && nla_len(a) >= (int)sizeof(uint64_t))
			memcpy(&tx, nla_data(a), sizeof(tx));
		else if (t == WGPEER_A_LAST_HANDSHAKE_TIME && nla_len(a) >= (int)sizeof(struct timespec)) {
			struct timespec ts;
			memcpy(&ts, nla_data(a), sizeof(ts));
			handshake = (uint64_t)ts.tv_sec;
		}
		p += NLA_ALIGN(a->nla_len);
		len -= NLA_ALIGN(a->nla_len);
	}
	if (pubkey[0])
		wg_emit_peer(device, pubkey, rx, tx, handshake);
}

static void wg_parse_device_msg(struct nlmsghdr *nh)
{
	struct genlmsghdr *gh = NLMSG_DATA(nh);
	char *attr = (char *)gh + GENL_HDRLEN;
	int alen = (int)(NLMSG_PAYLOAD(nh, 0) - GENL_HDRLEN);
	char device[64] = "";
	uint64_t listen_port = 0;
	uint64_t peers = 0;
	char *peers_attr = NULL;
	int peers_len = 0;

	while (alen >= (int)NLA_HDRLEN) {
		struct nlattr *a = (struct nlattr *)attr;
		if (a->nla_len < NLA_HDRLEN || a->nla_len > alen)
			break;
		int t = a->nla_type & NLA_TYPE_MASK;
		if (t == WGDEVICE_A_IFNAME && nla_len(a) > 0) {
			size_t n = (size_t)nla_len(a);
			if (n >= sizeof(device))
				n = sizeof(device) - 1;
			memcpy(device, nla_data(a), n);
			device[n] = '\0';
			device[strcspn(device, "\0")] = 0;
		} else if (t == WGDEVICE_A_LISTEN_PORT && nla_len(a) >= 2) {
			uint16_t port = 0;
			memcpy(&port, nla_data(a), sizeof(port));
			listen_port = port;
		} else if (t == WGDEVICE_A_PEERS) {
			peers_attr = (char *)nla_data(a);
			peers_len = nla_len(a);
		}
		attr += NLA_ALIGN(a->nla_len);
		alen -= NLA_ALIGN(a->nla_len);
	}
	if (!device[0])
		return;
	if (peers_attr) {
		int len = peers_len;
		char *p = peers_attr;
		while (len >= (int)NLA_HDRLEN) {
			struct nlattr *a = (struct nlattr *)p;
			if (a->nla_len < NLA_HDRLEN || a->nla_len > len)
				break;
			++peers;
			wg_parse_peer(device, (struct nlattr *)nla_data(a), nla_len(a));
			p += NLA_ALIGN(a->nla_len);
			len -= NLA_ALIGN(a->nla_len);
		}
	}
	wg_emit_device(device, listen_port, peers);
}

#ifndef WG_GENL_NAME
#define WG_GENL_NAME "wireguard"
#endif
#ifndef WG_GENL_VERSION
#define WG_GENL_VERSION 1
#endif
#ifndef WG_CMD_GET_DEVICE
#define WG_CMD_GET_DEVICE 0
#endif

static int wg_netlink_family(int fd)
{
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char payload[256];
	} req;
	memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	req.n.nlmsg_type = GENL_ID_CTRL;
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.n.nlmsg_seq = 1;
	req.g.cmd = CTRL_CMD_GETFAMILY;
	req.g.version = 1;

	struct nlattr *na = (struct nlattr *)((char *)&req + NLMSG_ALIGN(req.n.nlmsg_len));
	na->nla_type = CTRL_ATTR_FAMILY_NAME;
	size_t namelen = strlen(WG_GENL_NAME) + 1;
	na->nla_len = NLA_HDRLEN + namelen;
	memcpy((char *)na + NLA_HDRLEN, WG_GENL_NAME, namelen);
	req.n.nlmsg_len += NLMSG_ALIGN(na->nla_len);

	if (send(fd, &req, req.n.nlmsg_len, 0) < 0)
		return -1;

	char buf[4096];
	ssize_t n = recv(fd, buf, sizeof(buf), 0);
	if (n <= 0)
		return -1;
	struct nlmsghdr *nh = (struct nlmsghdr *)buf;
	if (!NLMSG_OK(nh, (unsigned)n) || nh->nlmsg_type == NLMSG_ERROR)
		return -1;
	struct genlmsghdr *gh = NLMSG_DATA(nh);
	char *attr = (char *)gh + GENL_HDRLEN;
	int alen = (int)(NLMSG_PAYLOAD(nh, 0) - GENL_HDRLEN);
	int family = -1;
	while (alen >= (int)NLA_HDRLEN) {
		struct nlattr *a = (struct nlattr *)attr;
		if (a->nla_len < NLA_HDRLEN || a->nla_len > alen)
			break;
		if (a->nla_type == CTRL_ATTR_FAMILY_ID)
			family = *(uint16_t *)((char *)a + NLA_HDRLEN);
		attr += NLA_ALIGN(a->nla_len);
		alen -= NLA_ALIGN(a->nla_len);
	}
	return family;
}

void get_wireguard_stats(void)
{
	char dump_path[512];
	snprintf(dump_path, sizeof(dump_path), "%s/net/wireguard_dump", ac->system_procfs);
	FILE *dfd = fopen(dump_path, "r");
	if (dfd) {
		char body[8192];
		size_t n = fread(body, 1, sizeof(body) - 1, dfd);
		fclose(dfd);
		body[n] = '\0';
		wireguard_parse_dump(body, n);
		return;
	}

	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	if (fd < 0) {
		if (errno != EPERM && errno != EACCES && errno != EPROTONOSUPPORT)
			carglog(ac->system_carg, L_DEBUG, "wireguard netlink socket: %s\n", strerror(errno));
		return;
	}
	int family = wg_netlink_family(fd);
	if (family < 0) {
		close(fd);
		return;
	}

	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
	} req;
	memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	req.n.nlmsg_type = (uint16_t)family;
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	req.n.nlmsg_seq = 2;
	req.g.cmd = WG_CMD_GET_DEVICE;
	req.g.version = WG_GENL_VERSION;
	if (send(fd, &req, req.n.nlmsg_len, 0) < 0) {
		close(fd);
		return;
	}

	char buf[16384];
	for (;;) {
		ssize_t n = recv(fd, buf, sizeof(buf), 0);
		if (n <= 0)
			break;
		struct nlmsghdr *nh = (struct nlmsghdr *)buf;
		int len = (int)n;
		int got_done = 0;
		for (; NLMSG_OK(nh, (unsigned)len); nh = NLMSG_NEXT(nh, len)) {
			if (nh->nlmsg_type == NLMSG_DONE || nh->nlmsg_type == NLMSG_ERROR) {
				got_done = 1;
				break;
			}
			wg_parse_device_msg(nh);
		}
		if (got_done)
			break;
	}
	close(fd);
}

#endif
