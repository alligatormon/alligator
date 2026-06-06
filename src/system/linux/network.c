#ifdef __linux__
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <net/if.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>
#include "main.h"
#include "metric/labels.h"
#include "common/logs.h"
#include "system/linux/network.h"

#define LINUXFS_LINE_LENGTH 300
#define NETLINK_STATS_BUF_SIZE 32768

#ifndef IFLA_SPEED
#define IFLA_SPEED 66
#endif

extern aconf *ac;

void network_if_stats_reset(network_if_stats *st)
{
	memset(st, 0, sizeof(*st));
}

static void network_if_stats_sysfs_speed(network_if_stats *st, const char *sysfs)
{
	char path[1000];
	int n;

	if (!sysfs || !st->valid || st->speed)
		return;

	n = snprintf(path, sizeof(path), "%s/class/net/%s/speed", sysfs, st->ifname);
	if (n <= 0 || (size_t)n >= sizeof(path))
		return;

	st->speed = getkvfile(path);
}

static void link_stats32_to64(struct rtnl_link_stats64 *s64, const struct rtnl_link_stats *s)
{
	s64->rx_packets = s->rx_packets;
	s64->tx_packets = s->tx_packets;
	s64->rx_bytes = s->rx_bytes;
	s64->tx_bytes = s->tx_bytes;
	s64->rx_errors = s->rx_errors;
	s64->tx_errors = s->tx_errors;
	s64->rx_dropped = s->rx_dropped;
	s64->tx_dropped = s->tx_dropped;
	s64->multicast = s->multicast;
	s64->collisions = s->collisions;
	s64->rx_frame_errors = s->rx_frame_errors;
	s64->rx_fifo_errors = s->rx_fifo_errors;
	s64->rx_compressed = s->rx_compressed;
	s64->tx_carrier_errors = s->tx_carrier_errors;
	s64->tx_fifo_errors = s->tx_fifo_errors;
	s64->tx_compressed = s->tx_compressed;
}

static uint8_t copy_ifla_ifname(char *dst, size_t dst_size, struct rtattr *attr)
{
	unsigned int payload = RTA_PAYLOAD(attr);
	const char *raw = RTA_DATA(attr);

	if (!payload || !dst_size)
		return 0;

	if (payload >= dst_size)
		payload = (unsigned int)(dst_size - 1);

	memcpy(dst, raw, payload);
	dst[payload] = 0;
	return 1;
}

void network_if_stats_emit_system(const network_if_stats *st, context_arg *carg)
{
	int64_t v;
	char *ifname;

	if (!st || !st->valid || !carg)
		return;

	ifname = (char *)st->ifname;

	v = (int64_t)st->rx_bytes; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "received_bytes");
	v = (int64_t)st->rx_packets; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "received_packets");
	v = (int64_t)st->rx_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "received_err");
	v = (int64_t)st->rx_dropped; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "received_drop");
	v = (int64_t)st->rx_fifo_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "received_fifo");
	v = (int64_t)st->rx_frame_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "received_frame");
	v = (int64_t)st->rx_compressed; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "received_compressed");
	v = (int64_t)st->multicast; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "received_multicast");
	v = (int64_t)st->tx_bytes; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "transmit_bytes");
	v = (int64_t)st->tx_packets; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "transmit_packets");
	v = (int64_t)st->tx_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "transmit_err");
	v = (int64_t)st->tx_dropped; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "transmit_drop");
	v = (int64_t)st->tx_fifo_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "transmit_fifo");
	v = (int64_t)st->collisions; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "transmit_colls");
	v = (int64_t)st->tx_carrier_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "transmit_carrier");
	v = (int64_t)st->tx_compressed; metric_add_labels2("if_stat", &v, DATATYPE_INT, carg, "ifname", ifname, "type", "transmit_compressed");

	v = st->speed;
	metric_add_labels("if_speed", &v, DATATYPE_INT, carg, "ifname", ifname);
}

typedef struct network_netlink_ctx {
	network_if_stats_cb cb;
	void *arg;
	uint8_t skip_veth;
	context_arg *log_carg;
	const char *sysfs;
} network_netlink_ctx;

static int network_netlink_parse_link(const char *ifname, struct rtnl_link_stats64 *s64, uint8_t has_stats, uint32_t speed, network_netlink_ctx *ctx)
{
	network_if_stats st;

	if (!has_stats)
		return 0;
	if (ctx->skip_veth && !strncmp(ifname, "veth", 4))
		return 0;

	memset(&st, 0, sizeof(st));
	strlcpy(st.ifname, ifname, sizeof(st.ifname));
	st.rx_bytes = s64->rx_bytes;
	st.rx_packets = s64->rx_packets;
	st.rx_errors = s64->rx_errors;
	st.rx_dropped = s64->rx_dropped;
	st.rx_fifo_errors = s64->rx_fifo_errors;
	st.rx_frame_errors = s64->rx_frame_errors;
	st.rx_compressed = s64->rx_compressed;
	st.multicast = s64->multicast;
	st.tx_bytes = s64->tx_bytes;
	st.tx_packets = s64->tx_packets;
	st.tx_errors = s64->tx_errors;
	st.tx_dropped = s64->tx_dropped;
	st.tx_fifo_errors = s64->tx_fifo_errors;
	st.collisions = s64->collisions;
	st.tx_carrier_errors = s64->tx_carrier_errors;
	st.tx_compressed = s64->tx_compressed;
	st.speed = speed;
	st.valid = 1;
	network_if_stats_sysfs_speed(&st, ctx->sysfs);
	return ctx->cb(&st, ctx->arg);
}

static int network_netlink_recv_dump(int fd, network_netlink_ctx *ctx)
{
	char buf[NETLINK_STATS_BUF_SIZE];

	for (;;)
	{
		ssize_t len = recv(fd, buf, sizeof(buf), 0);
		if (len < 0)
		{
			if (errno == EINTR)
				continue;
			if (errno != 0)
				carglog(ctx->log_carg, L_ERROR, "network_netlink: recv: %s\n", strerror(errno));
			return -1;
		}
		if (len == 0)
			break;

		struct nlmsghdr *nh;
		for (nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, (unsigned int)len); nh = NLMSG_NEXT(nh, len))
		{
			if (nh->nlmsg_type == NLMSG_DONE)
				return 0;
			if (nh->nlmsg_type == NLMSG_ERROR)
			{
				struct nlmsgerr *err = NLMSG_DATA(nh);
				if (err->error)
					carglog(ctx->log_carg, L_ERROR, "network_netlink: netlink error %d\n", err->error);
				return -1;
			}
			if (nh->nlmsg_type != RTM_NEWLINK)
				continue;

			struct ifinfomsg *ifi = NLMSG_DATA(nh);
			int rtl = (int)(nh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifi)));
			if (rtl < 0)
				continue;

			char ifname[IFNAMSIZ];
			uint8_t has_ifname = 0;
			struct rtnl_link_stats64 s64;
			uint32_t link_speed = 0;
			memset(&s64, 0, sizeof(s64));
			uint8_t has_stats = 0;

			struct rtattr *attr;
			for (attr = IFLA_RTA(ifi); RTA_OK(attr, rtl); attr = RTA_NEXT(attr, rtl))
			{
				if (attr->rta_type == IFLA_IFNAME)
					has_ifname = copy_ifla_ifname(ifname, sizeof(ifname), attr);
				else if (attr->rta_type == IFLA_STATS64 && RTA_PAYLOAD(attr) >= sizeof(struct rtnl_link_stats64))
				{
					memcpy(&s64, RTA_DATA(attr), sizeof(struct rtnl_link_stats64));
					has_stats = 1;
				}
				else if (!has_stats && attr->rta_type == IFLA_STATS && RTA_PAYLOAD(attr) >= sizeof(struct rtnl_link_stats))
				{
					link_stats32_to64(&s64, (struct rtnl_link_stats *)RTA_DATA(attr));
					has_stats = 1;
				}
				else if (attr->rta_type == IFLA_SPEED && RTA_PAYLOAD(attr) >= sizeof(uint32_t))
				{
					memcpy(&link_speed, RTA_DATA(attr), sizeof(uint32_t));
					if (link_speed == UINT32_MAX)
						link_speed = 0;
				}
			}

			if (!has_ifname)
				continue;

			if (network_netlink_parse_link(ifname, &s64, has_stats, link_speed, ctx))
				return 1;
		}
	}

	return 0;
}

int network_netlink_foreach(network_if_stats_cb cb, void *arg, uint8_t skip_veth, context_arg *log_carg, const char *sysfs)
{
	int fd;
	struct sockaddr_nl addr;
	socklen_t addr_len = sizeof(addr);
	network_netlink_ctx ctx;
	int rc;

	if (!cb)
		return -1;

	ctx.cb = cb;
	ctx.arg = arg;
	ctx.skip_veth = skip_veth;
	ctx.log_carg = log_carg;
	ctx.sysfs = sysfs;

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (fd < 0)
	{
		if (ctx.log_carg)
			carglog(ctx.log_carg, L_ERROR, "network_netlink: socket: %s\n", strerror(errno));
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		if (ctx.log_carg)
			carglog(ctx.log_carg, L_ERROR, "network_netlink: bind: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	if (getsockname(fd, (struct sockaddr *)&addr, &addr_len) < 0)
	{
		if (ctx.log_carg)
			carglog(ctx.log_carg, L_ERROR, "network_netlink: getsockname: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	struct {
		struct nlmsghdr nlh;
		struct ifinfomsg ifm;
	} req;

	memset(&req, 0, sizeof(req));
	req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nlh.nlmsg_type = RTM_GETLINK;
	req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	req.nlh.nlmsg_seq = 1;
	req.nlh.nlmsg_pid = addr.nl_pid;
	req.ifm.ifi_family = AF_UNSPEC;

	for (;;)
	{
		ssize_t sent = send(fd, &req, req.nlh.nlmsg_len, 0);
		if (sent < 0)
		{
			if (errno == EINTR)
				continue;
			if (ctx.log_carg)
				carglog(ctx.log_carg, L_ERROR, "network_netlink: send: %s\n", strerror(errno));
			close(fd);
			return -1;
		}
		break;
	}

	rc = network_netlink_recv_dump(fd, &ctx);
	close(fd);
	return rc;
}

typedef struct network_lookup_ctx {
	const char *ifname;
	network_if_stats *st;
	uint8_t found;
} network_lookup_ctx;

static int network_lookup_cb(const network_if_stats *st, void *arg)
{
	network_lookup_ctx *ctx = arg;

	if (strcmp(st->ifname, ctx->ifname) != 0)
		return 0;

	*ctx->st = *st;
	ctx->found = 1;
	return 1;
}

int network_if_stats_netlink(const char *ifname, network_if_stats *st)
{
	network_lookup_ctx ctx;

	if (!ifname || !st)
		return -1;

	memset(st, 0, sizeof(*st));
	ctx.ifname = ifname;
	ctx.st = st;
	ctx.found = 0;

	if (network_netlink_foreach(network_lookup_cb, &ctx, 0, ac ? ac->system_carg : NULL, ac ? ac->system_sysfs : NULL) < 0)
		return -1;

	return ctx.found ? 0 : -1;
}

static int network_emit_system_cb(const network_if_stats *st, void *arg)
{
	network_if_stats_emit_system(st, arg);
	return 0;
}

void get_network_statistics_netlink()
{
	carglog(ac->system_carg, L_INFO, "system scrape metrics: network: network_statistics (netlink)\n");
	network_netlink_foreach(network_emit_system_cb, ac->system_carg, 1, ac->system_carg, ac->system_sysfs);
}

void interface_stats()
{
	carglog(ac->system_carg, L_INFO, "system scrape metrics: network: interface_stats\n");

	struct dirent *entry;
	DIR *dp;

	char classpath[255];
	snprintf(classpath, 255, "%s/class/net", ac->system_sysfs);
	dp = opendir(classpath);
	if (!dp)
		return;

	while((entry = readdir(dp)))
	{
		if ( entry->d_name[0] == '.' )
			continue;

		if (!strncmp(entry->d_name, "veth", 4))
			continue;

		char operfile[512];
		char operstate[100];
		snprintf(operfile, 511, "%s/class/net/%s/operstate", ac->system_sysfs, entry->d_name);
		FILE *fd = fopen(operfile, "r");
		if (!fd)
			continue;

		if(!fgets(operstate, 100, fd))
		{
			fclose(fd);
			continue;
		}
		fclose(fd);
		operstate[strlen(operstate)-1] = 0;

		int64_t vl = 1;
		metric_add_labels2("link_status", &vl, DATATYPE_INT, ac->system_carg, "ifname", entry->d_name, "state", operstate);
	}
	closedir(dp);
}
#endif
