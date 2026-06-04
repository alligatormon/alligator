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

#define LINUXFS_LINE_LENGTH 300
#define NETLINK_STATS_BUF_SIZE 32768

extern aconf *ac;

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

/* Netlink RTM_GETLINK backend; production scrape uses get_network_statistics() (/proc). */
void get_network_statistics2()
{
	carglog(ac->system_carg, L_INFO, "system scrape metrics: network: network_statistics (netlink)\n");

	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (fd < 0)
	{
		carglog(ac->system_carg, L_ERROR, "network_statistics2: socket: %s\n", strerror(errno));
		return;
	}

	struct sockaddr_nl addr;
	socklen_t addr_len = sizeof(addr);

	memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		carglog(ac->system_carg, L_ERROR, "network_statistics2: bind: %s\n", strerror(errno));
		close(fd);
		return;
	}
	if (getsockname(fd, (struct sockaddr *)&addr, &addr_len) < 0)
	{
		carglog(ac->system_carg, L_ERROR, "network_statistics2: getsockname: %s\n", strerror(errno));
		close(fd);
		return;
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
			carglog(ac->system_carg, L_ERROR, "network_statistics2: send: %s\n", strerror(errno));
			close(fd);
			return;
		}
		break;
	}

	char buf[NETLINK_STATS_BUF_SIZE];
	for (;;)
	{
		ssize_t len = recv(fd, buf, sizeof(buf), 0);
		if (len < 0)
		{
			if (errno == EINTR)
				continue;
			if (errno != 0)
				carglog(ac->system_carg, L_ERROR, "network_statistics2: recv: %s\n", strerror(errno));
			break;
		}
		if (len == 0)
			break;

		struct nlmsghdr *nh;
		for (nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, (unsigned int)len); nh = NLMSG_NEXT(nh, len))
		{
			if (nh->nlmsg_type == NLMSG_DONE)
			{
				close(fd);
				return;
			}
			if (nh->nlmsg_type == NLMSG_ERROR)
			{
				struct nlmsgerr *err = NLMSG_DATA(nh);
				if (err->error)
					carglog(ac->system_carg, L_ERROR, "network_statistics2: netlink error %d\n", err->error);
				close(fd);
				return;
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
			}

			if (!has_ifname || !has_stats)
				continue;
			if (!strncmp(ifname, "veth", 4))
				continue;

			int64_t v;
			v = (int64_t)s64.rx_bytes; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_bytes");
			v = (int64_t)s64.rx_packets; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_packets");
			v = (int64_t)s64.rx_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_err");
			v = (int64_t)s64.rx_dropped; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_drop");
			v = (int64_t)s64.rx_fifo_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_fifo");
			v = (int64_t)s64.rx_frame_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_frame");
			v = (int64_t)s64.rx_compressed; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_compressed");
			v = (int64_t)s64.multicast; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_multicast");
			v = (int64_t)s64.tx_bytes; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_bytes");
			v = (int64_t)s64.tx_packets; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_packets");
			v = (int64_t)s64.tx_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_err");
			v = (int64_t)s64.tx_dropped; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_drop");
			v = (int64_t)s64.tx_fifo_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_fifo");
			v = (int64_t)s64.collisions; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_colls");
			v = (int64_t)s64.tx_carrier_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_carrier");
			v = (int64_t)s64.tx_compressed; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_compressed");

			char ifdir[1000];
			int path_len = snprintf(ifdir, sizeof(ifdir), "%s/class/net/%s/speed", ac->system_sysfs, ifname);
			if (path_len > 0 && (size_t)path_len < sizeof(ifdir))
			{
				int64_t interface_speed_bits = getkvfile(ifdir);
				metric_add_labels("if_speed", &interface_speed_bits, DATATYPE_INT, ac->system_carg, "ifname", ifname);
			}
		}
	}

	close(fd);
}

void get_network_statistics()
{
	carglog(ac->system_carg, L_INFO, "system scrape metrics: network: network_statistics\n");

	int64_t received_bytes;
	int64_t received_packets;
	int64_t received_err;
	int64_t received_drop;
	int64_t received_fifo;
	int64_t received_frame;
	int64_t received_compressed;
	int64_t received_multicast;

	int64_t transmit_bytes;
	int64_t transmit_packets;
	int64_t transmit_err;
	int64_t transmit_drop;
	int64_t transmit_fifo;
	int64_t transmit_colls;
	int64_t transmit_carrier;
	int64_t transmit_compressed;

	char ifdir[1000];
	char procnetdev[255];
	snprintf(procnetdev, 255, "%s/net/dev", ac->system_procfs);
	FILE *fp = fopen(procnetdev, "r");
	if (!fp)
		return;
	char buf[200], ifname[20];

	int i;
	for (i = 0; i < 2; i++) {
		if(!fgets(buf, 200, fp))
		{
			fclose(fp);
			return;
		}
	}

	for (i=0; fgets(buf, 200, fp); i++)
	{
		int from = strspn(buf, " ");
		int to = strcspn(buf+from, ":");

		if (!strncmp(buf+from, "veth", 4))
			continue;
		strlcpy(ifname, buf+from, to+1);

		char *pEnd;
		pEnd = buf+from+to + strcspn(buf+from+to, "\t ");
		pEnd += strspn(pEnd, "\t ");
		received_bytes = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_bytes, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_bytes");

		received_packets = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_packets, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_packets");

		received_err = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_err, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_err");

		received_drop = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_drop, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_drop");

		received_fifo = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_fifo, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_fifo");

		received_frame = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_frame, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_frame");

		received_compressed = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_compressed, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_compressed");

		received_multicast = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &received_multicast, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "received_multicast");

		transmit_bytes = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_bytes, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_bytes");

		transmit_packets = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_packets, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_packets");

		transmit_err = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_err, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_err");

		transmit_drop = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_drop, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_drop");

		transmit_fifo = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_fifo, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_fifo");

		transmit_colls = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_colls, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_colls");

		transmit_carrier = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_carrier, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_carrier");

		transmit_compressed = strtoll(pEnd, &pEnd, 10);
		metric_add_labels2("if_stat", &transmit_compressed, DATATYPE_INT, ac->system_carg, "ifname", ifname, "type", "transmit_compressed");

		snprintf(ifdir, 1000, "%s/class/net/%s/speed", ac->system_sysfs, ifname);
		int64_t interface_speed_bits = getkvfile(ifdir);
		metric_add_labels("if_speed", &interface_speed_bits, DATATYPE_INT, ac->system_carg, "ifname", ifname);
	}

	fclose(fp);
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
