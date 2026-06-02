#ifdef __linux__
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>
#include "main.h"
#include "metric/labels.h"
#include "common/logs.h"

#define LINUXFS_LINE_LENGTH 300

extern aconf *ac;

void get_network_statistics()
{
	carglog(ac->system_carg, L_INFO, "system scrape metrics: network: network_statistics\n");
	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (fd < 0)
		return;

	struct {
		struct nlmsghdr nlh;
		struct ifinfomsg ifm;
	} req;

	memset(&req, 0, sizeof(req));
	req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nlh.nlmsg_type = RTM_GETLINK;
	req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	req.nlh.nlmsg_seq = 1;
	req.nlh.nlmsg_pid = 0;
	req.ifm.ifi_family = AF_UNSPEC;

	if (send(fd, &req, req.nlh.nlmsg_len, 0) < 0)
	{
		close(fd);
		return;
	}

	char buf[32768];
	for (;;)
	{
		ssize_t len = recv(fd, buf, sizeof(buf), 0);
		if (len <= 0)
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
				close(fd);
				return;
			}
			if (nh->nlmsg_type != RTM_NEWLINK)
				continue;

			struct ifinfomsg *ifi = NLMSG_DATA(nh);
			int rtl = nh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifi));
			if (rtl < 0)
				continue;

			struct rtattr *attr;
			const char *ifname = NULL;
			struct rtnl_link_stats64 s64;
			memset(&s64, 0, sizeof(s64));
			uint8_t has_stats = 0;

			for (attr = IFLA_RTA(ifi); RTA_OK(attr, rtl); attr = RTA_NEXT(attr, rtl))
			{
				if (attr->rta_type == IFLA_IFNAME)
					ifname = (const char *)RTA_DATA(attr);
				else if (attr->rta_type == IFLA_STATS64 && RTA_PAYLOAD(attr) >= sizeof(struct rtnl_link_stats64))
				{
					memcpy(&s64, RTA_DATA(attr), sizeof(struct rtnl_link_stats64));
					has_stats = 1;
				}
				else if (attr->rta_type == IFLA_STATS && RTA_PAYLOAD(attr) >= sizeof(struct rtnl_link_stats))
				{
					struct rtnl_link_stats *s = (struct rtnl_link_stats *)RTA_DATA(attr);
					s64.rx_packets = s->rx_packets;
					s64.tx_packets = s->tx_packets;
					s64.rx_bytes = s->rx_bytes;
					s64.tx_bytes = s->tx_bytes;
					s64.rx_errors = s->rx_errors;
					s64.tx_errors = s->tx_errors;
					s64.rx_dropped = s->rx_dropped;
					s64.tx_dropped = s->tx_dropped;
					s64.multicast = s->multicast;
					s64.collisions = s->collisions;
					s64.rx_frame_errors = s->rx_frame_errors;
					s64.rx_fifo_errors = s->rx_fifo_errors;
					s64.rx_compressed = s->rx_compressed;
					s64.tx_carrier_errors = s->tx_carrier_errors;
					s64.tx_fifo_errors = s->tx_fifo_errors;
					s64.tx_compressed = s->tx_compressed;
					has_stats = 1;
				}
			}

			if (!ifname || !has_stats)
				continue;
			if (!strncmp(ifname, "veth", 4))
				continue;

			int64_t v;
			v = (int64_t)s64.rx_bytes; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "received_bytes");
			v = (int64_t)s64.rx_packets; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "received_packets");
			v = (int64_t)s64.rx_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "received_err");
			v = (int64_t)s64.rx_dropped; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "received_drop");
			v = (int64_t)s64.rx_fifo_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "received_fifo");
			v = (int64_t)s64.rx_frame_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "received_frame");
			v = (int64_t)s64.rx_compressed; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "received_compressed");
			v = (int64_t)s64.multicast; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "received_multicast");
			v = (int64_t)s64.tx_bytes; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "transmit_bytes");
			v = (int64_t)s64.tx_packets; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "transmit_packets");
			v = (int64_t)s64.tx_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "transmit_err");
			v = (int64_t)s64.tx_dropped; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "transmit_drop");
			v = (int64_t)s64.tx_fifo_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "transmit_fifo");
			v = (int64_t)s64.collisions; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "transmit_colls");
			v = (int64_t)s64.tx_carrier_errors; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "transmit_carrier");
			v = (int64_t)s64.tx_compressed; metric_add_labels2("if_stat", &v, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname, "type", "transmit_compressed");

			char ifdir[1000];
			snprintf(ifdir, 1000, "%s/class/net/%s/speed", ac->system_sysfs, ifname);
			int64_t interface_speed_bits = getkvfile(ifdir);
			metric_add_labels("if_speed", &interface_speed_bits, DATATYPE_INT, ac->system_carg, "ifname", (char*)ifname);
		}
	}

	close(fd);
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
