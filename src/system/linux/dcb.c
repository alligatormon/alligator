#ifdef __linux__

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/dcbnl.h>
#include "main.h"
#include "common/logs.h"
#include "system/common.h"
#include "system/linux/dcb.h"

extern aconf *ac;

#define DCB_NL_BUF 8192

#ifndef RTM_GETDCB
#define RTM_GETDCB 78
#endif
#ifndef DCB_CMD_IEEE_GET
#define DCB_CMD_IEEE_GET 21
#endif
#ifndef DCB_ATTR_IFNAME
#define DCB_ATTR_IFNAME 1
#endif
#ifndef DCB_ATTR_IEEE
#define DCB_ATTR_IEEE 13
#endif
#ifndef DCB_ATTR_IEEE_PFC
#define DCB_ATTR_IEEE_PFC 2
#endif
#ifndef IEEE_8021QAZ_MAX_TCS
#define IEEE_8021QAZ_MAX_TCS 8
#endif

#ifndef NLA_TYPE_MASK
#define NLA_TYPE_MASK 0x3fff
#endif

static void dcb_emit_pfc(const char *device, const struct ieee_pfc *pfc)
{
	char prio[8];
	int i;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; ++i) {
		uint64_t sent = pfc->requests[i];
		uint64_t recv = pfc->indications[i];
		snprintf(prio, sizeof(prio), "%d", i);
		metric_add_labels2("dcb_pfc_sent_total", &sent, DATATYPE_UINT, ac->system_carg,
			"device", (char *)device, "prio", prio);
		metric_add_labels2("dcb_pfc_received_total", &recv, DATATYPE_UINT, ac->system_carg,
			"device", (char *)device, "prio", prio);
	}
}

static int dcb_find_pfc(struct rtattr *attrs, int rem, struct ieee_pfc *out)
{
	struct rtattr *attr;

	for (attr = attrs; RTA_OK(attr, rem); attr = RTA_NEXT(attr, rem)) {
		uint16_t type = attr->rta_type & NLA_TYPE_MASK;
		if (type != DCB_ATTR_IEEE)
			continue;

		int nested_rem = RTA_PAYLOAD(attr);
		struct rtattr *nattr;
		for (nattr = RTA_DATA(attr); RTA_OK(nattr, nested_rem); nattr = RTA_NEXT(nattr, nested_rem)) {
			uint16_t ntype = nattr->rta_type & NLA_TYPE_MASK;
			if (ntype != DCB_ATTR_IEEE_PFC)
				continue;
			if (RTA_PAYLOAD(nattr) < sizeof(struct ieee_pfc))
				return 0;
			memcpy(out, RTA_DATA(nattr), sizeof(*out));
			return 1;
		}
	}
	return 0;
}

int dcb_parse_ieee_emit(const char *device, const void *payload, size_t len)
{
	struct ieee_pfc pfc;
	const struct dcbmsg *dcb;
	struct rtattr *attrs;
	int rem;

	if (!device || !device[0] || !payload || len < sizeof(struct dcbmsg))
		return 0;

	dcb = payload;
	(void)dcb;
	attrs = (struct rtattr *)((const char *)payload + NLMSG_ALIGN(sizeof(struct dcbmsg)));
	rem = (int)(len - NLMSG_ALIGN(sizeof(struct dcbmsg)));
	if (rem < (int)sizeof(struct rtattr))
		return 0;

	memset(&pfc, 0, sizeof(pfc));
	if (!dcb_find_pfc(attrs, rem, &pfc))
		return 0;

	dcb_emit_pfc(device, &pfc);
	return 1;
}

static int dcb_add_attr(char *buf, size_t bufsize, size_t *used, uint16_t type, const void *data, size_t dlen)
{
	size_t alen = RTA_LENGTH(dlen);
	struct rtattr *rta;

	if (*used + alen > bufsize)
		return -1;
	rta = (struct rtattr *)(buf + *used);
	rta->rta_type = type;
	rta->rta_len = (unsigned short)alen;
	if (dlen && data)
		memcpy(RTA_DATA(rta), data, dlen);
	*used += RTA_ALIGN(alen);
	return 0;
}

static int dcb_query_device(int fd, const char *ifname)
{
	char reqbuf[256];
	size_t used;
	struct nlmsghdr *nlh;
	struct dcbmsg *dcb;
	char resp[DCB_NL_BUF];
	ssize_t rlen;

	memset(reqbuf, 0, sizeof(reqbuf));
	nlh = (struct nlmsghdr *)reqbuf;
	dcb = (struct dcbmsg *)(reqbuf + sizeof(*nlh));
	dcb->dcb_family = AF_UNSPEC;
	dcb->cmd = DCB_CMD_IEEE_GET;
	dcb->dcb_pad = 0;
	used = sizeof(*nlh) + NLMSG_ALIGN(sizeof(*dcb));

	if (dcb_add_attr(reqbuf, sizeof(reqbuf), &used, DCB_ATTR_IFNAME, ifname, strlen(ifname) + 1) < 0)
		return -1;

	nlh->nlmsg_len = (uint32_t)used;
	nlh->nlmsg_type = RTM_GETDCB;
	nlh->nlmsg_flags = NLM_F_REQUEST;
	nlh->nlmsg_seq = 1;

	for (;;) {
		ssize_t sent = send(fd, reqbuf, used, 0);
		if (sent < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		break;
	}

	for (;;) {
		rlen = recv(fd, resp, sizeof(resp), 0);
		if (rlen < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		break;
	}

	{
		struct nlmsghdr *nh;
		int remaining = (int)rlen;
		for (nh = (struct nlmsghdr *)resp; NLMSG_OK(nh, remaining); nh = NLMSG_NEXT(nh, remaining)) {
			if (nh->nlmsg_type == NLMSG_DONE)
				continue;
			if (nh->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *err = NLMSG_DATA(nh);
				int e = -err->error;
				if (!err->error)
					continue;
				if (e == EOPNOTSUPP || e == ENODEV || e == EINVAL)
					return 0;
				return -1;
			}

			size_t payload_len = nh->nlmsg_len - NLMSG_HDRLEN;
			if (payload_len < sizeof(struct dcbmsg))
				continue;
			dcb_parse_ieee_emit(ifname, NLMSG_DATA(nh), payload_len);
		}
	}
	return 0;
}

void get_dcb_pfc_stats(void)
{
	char root[512];
	DIR *dir;
	struct dirent *ent;
	int fd;
	struct sockaddr_nl addr;

	snprintf(root, sizeof(root), "%s/class/net", ac->system_sysfs);
	dir = opendir(root);
	if (!dir)
		return;

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (fd < 0) {
		closedir(dir);
		return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		closedir(dir);
		return;
	}

	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		if (!strcmp(ent->d_name, "lo") || system_iface_is_veth(ent->d_name))
			continue;
		dcb_query_device(fd, ent->d_name);
	}

	close(fd);
	closedir(dir);
}

#endif
