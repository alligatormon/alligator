#ifdef __APPLE__
#include "main.h"
#include "common/logs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_media.h>
#include <netinet/in.h>
#include <netinet/ip_var.h>
#include <netinet/tcp_var.h>
#include <netinet/udp_var.h>
#include <netinet/icmp_var.h>
#include <netinet/ip_icmp.h>
#include <sys/sysctl.h>

extern aconf *ac;

static void emit_stat(const char *proto, const char *stat, int64_t val)
{
	metric_add_labels2("network_stat_total", &val, DATATYPE_INT, ac->system_carg, "proto", (char *)proto, "stat", (char *)stat);
}

static void get_netstat_statistics(void)
{
	struct tcpstat tcpstat;
	struct udpstat udpstat;
	struct ipstat ipstat;
	struct icmpstat icmpstat;
	size_t len;

	len = sizeof(tcpstat);
	if (sysctlbyname("net.inet.tcp.stats", &tcpstat, &len, NULL, 0) == 0) {
		emit_stat("Tcp", "ConnAttempt", (int64_t)tcpstat.tcps_connattempt);
		emit_stat("Tcp", "Accepts", (int64_t)tcpstat.tcps_accepts);
		emit_stat("Tcp", "Connects", (int64_t)tcpstat.tcps_connects);
		emit_stat("Tcp", "Drops", (int64_t)tcpstat.tcps_drops);
		emit_stat("Tcp", "Conndrops", (int64_t)tcpstat.tcps_conndrops);
		emit_stat("Tcp", "Closed", (int64_t)tcpstat.tcps_closed);
		emit_stat("Tcp", "Segstimed", (int64_t)tcpstat.tcps_segstimed);
		emit_stat("Tcp", "Received", (int64_t)tcpstat.tcps_rcvpack);
		emit_stat("Tcp", "Sent", (int64_t)tcpstat.tcps_sndpack);
	}

	len = sizeof(udpstat);
	if (sysctlbyname("net.inet.udp.stats", &udpstat, &len, NULL, 0) == 0) {
		emit_stat("Udp", "InDatagrams", (int64_t)udpstat.udps_ipackets);
		emit_stat("Udp", "OutDatagrams", (int64_t)udpstat.udps_opackets);
		emit_stat("Udp", "NoPorts", (int64_t)udpstat.udps_noport);
		emit_stat("Udp", "InErrors", (int64_t)udpstat.udps_badlen + udpstat.udps_badsum);
	}

	len = sizeof(ipstat);
	if (sysctlbyname("net.inet.ip.stats", &ipstat, &len, NULL, 0) == 0) {
		emit_stat("Ip", "InReceives", (int64_t)ipstat.ips_total);
		emit_stat("Ip", "InDelivers", (int64_t)ipstat.ips_delivered);
		emit_stat("Ip", "OutRequests", (int64_t)ipstat.ips_localout);
		emit_stat("Ip", "OutDiscards", (int64_t)ipstat.ips_odropped);
		emit_stat("Ip", "ReasmReqds", (int64_t)ipstat.ips_fragmented);
		emit_stat("Ip", "InHdrErrors", (int64_t)ipstat.ips_badvers + ipstat.ips_badhlen);
	}

	len = sizeof(icmpstat);
	if (sysctlbyname("net.inet.icmp.stats", &icmpstat, &len, NULL, 0) == 0) {
		int64_t inmsgs = 0, outmsgs = 0;
		int i;

		for (i = 0; i <= ICMP_MAXTYPE; i++) {
			inmsgs += (int64_t)icmpstat.icps_inhist[i];
			outmsgs += (int64_t)icmpstat.icps_outhist[i];
		}
		emit_stat("Icmp", "InMsgs", inmsgs);
		emit_stat("Icmp", "OutMsgs", outmsgs);
		emit_stat("Icmp", "Errors", (int64_t)icmpstat.icps_error);
	}
}

static void get_interface_media(void)
{
	struct if_nameindex *ifni, *ifp;
	int sock;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		return;

	ifni = if_nameindex();
	if (!ifni) {
		close(sock);
		return;
	}

	for (ifp = ifni; ifp->if_index != 0 || ifp->if_name != NULL; ifp++) {
		struct ifmediareq ifmr;
		int64_t val;

		if (!ifp->if_name)
			break;

		memset(&ifmr, 0, sizeof(ifmr));
		strlcpy(ifmr.ifm_name, ifp->if_name, sizeof(ifmr.ifm_name));
		if (ioctl(sock, SIOCGIFMEDIA, &ifmr) != 0)
			continue;

		val = (ifmr.ifm_active & IFM_FDX) ? 1 : 0;
		metric_add_labels("if_duplex", &val, DATATYPE_INT, ac->system_carg, "ifname", ifp->if_name);
	}

	if_freenameindex(ifni);
	close(sock);
}

void get_network_statistics(void)
{
	get_netstat_statistics();
	get_interface_media();
}
#endif
