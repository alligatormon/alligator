#ifdef __FreeBSD__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/param.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/tcp_var.h>
#include <netinet/udp_var.h>
#include <netinet/tcp_fsm.h>
#include <netinet/in_pcb.h>
#ifndef ICMP_MAXTYPE
#define ICMP_MAXTYPE 40
#endif
#include <netinet/icmp_var.h>
#include "main.h"
#include "common/logs.h"

#ifndef SA_SIZE
#define SA_SIZE(sa)						\
	((!(sa) || ((const struct sockaddr *)(sa))->sa_len == 0) ?	\
		sizeof(long) :						\
		1 + ((((const struct sockaddr *)(sa))->sa_len - 1) | (sizeof(long) - 1)))
#endif

extern aconf *ac;

static void emit_stat(char *proto, char *stat, int64_t val)
{
	metric_add_labels2("network_stat_total", &val, DATATYPE_INT, ac->system_carg, "proto", proto, "stat", stat);
}

void get_netstat_statistics(void)
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

void get_interface_media(void)
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

void get_arp_stats(void)
{
	int mib[6];
	size_t needed = 0;
	char *buf = NULL, *next, *lim;
	char devices[64][IFNAMSIZ];
	uint64_t counts[64];
	size_t ndev = 0;
	size_t i;
	int attempt;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_INET;
	mib[4] = NET_RT_FLAGS;
	mib[5] = RTF_LLINFO;

	for (attempt = 0; attempt < 3; attempt++) {
		needed = 0;
		if (sysctl(mib, 6, NULL, &needed, NULL, 0) != 0 || needed == 0) {
			free(buf);
			return;
		}
		needed += needed / 8 + 1024;
		free(buf);
		buf = malloc(needed);
		if (!buf)
			return;
		if (sysctl(mib, 6, buf, &needed, NULL, 0) == 0)
			break;
		if (errno != ENOMEM) {
			free(buf);
			return;
		}
	}
	if (attempt >= 3) {
		free(buf);
		return;
	}

	lim = buf + needed;
	for (next = buf; next < lim; ) {
		struct rt_msghdr *rtm;
		struct sockaddr *sa;
		struct sockaddr_dl *sdl = NULL;
		char ifname[IFNAMSIZ];
		char *cp;
		char *msg_end;
		int addrs;

		if ((size_t)(lim - next) < sizeof(*rtm))
			break;
		rtm = (struct rt_msghdr *)(void *)next;
		if (rtm->rtm_msglen < sizeof(*rtm) || next + rtm->rtm_msglen > lim)
			break;
		msg_end = next + rtm->rtm_msglen;
		next = msg_end;

		/* Sockaddrs live inside this message only — never walk into the next rtm. */
		cp = (char *)(rtm + 1);
		addrs = rtm->rtm_addrs;
		for (i = 0; i < RTAX_MAX; i++) {
			size_t sa_sz;
			size_t remain;

			if (!(addrs & (1 << i)))
				continue;
			remain = (size_t)(msg_end - cp);
			/* Need sa_len + sa_family before SA_SIZE / family checks. */
			if (remain < 2)
				break;
			sa = (struct sockaddr *)(void *)cp;
			sa_sz = SA_SIZE(sa);
			if (sa_sz < 2 || sa_sz > remain)
				break;
			if (i == RTAX_GATEWAY && sa->sa_family == AF_LINK &&
				sa->sa_len >= offsetof(struct sockaddr_dl, sdl_data))
				sdl = (struct sockaddr_dl *)(void *)sa;
			cp += sa_sz;
		}
		if (!sdl)
			continue;

		ifname[0] = '\0';
		if (sdl->sdl_index)
			if_indextoname(sdl->sdl_index, ifname);
		if (!ifname[0] && sdl->sdl_nlen > 0 && sdl->sdl_nlen < (int)sizeof(ifname)) {
			size_t name_off = offsetof(struct sockaddr_dl, sdl_data);
			if ((size_t)sdl->sdl_len >= name_off + (size_t)sdl->sdl_nlen) {
				memcpy(ifname, sdl->sdl_data, (size_t)sdl->sdl_nlen);
				ifname[sdl->sdl_nlen] = '\0';
			}
		}
		if (!ifname[0])
			continue;

		for (i = 0; i < ndev; i++) {
			if (!strcmp(devices[i], ifname))
				break;
		}
		if (i == ndev && ndev < 64) {
			strlcpy(devices[ndev], ifname, sizeof(devices[ndev]));
			counts[ndev] = 0;
			++ndev;
		}
		if (i < ndev)
			++counts[i];
	}
	free(buf);

	for (i = 0; i < ndev; i++)
		metric_add_labels("arp_entries", &counts[i], DATATYPE_UINT,
			ac->system_carg, "device", devices[i]);
}

static void sockstat_emit_u64(const char *protocol, const char *stat, uint64_t val)
{
	metric_add_labels2("sockstat_stat_total", &val, DATATYPE_UINT,
		ac->system_carg, "protocol", (char *)protocol, "stat", (char *)stat);
}

/* Returns 0 on success (count may be 0), -1 if sysctl unavailable. */
static int sockstat_pcblist_count(const char *sysctl_name, uint64_t *count_out)
{
	size_t len = 0;
	char *pbuf = NULL;
	struct xinpgen *xig;
	int attempt;

	*count_out = 0;
	for (attempt = 0; attempt < 3; attempt++) {
		len = 0;
		if (sysctlbyname(sysctl_name, NULL, &len, NULL, 0) != 0 || len < sizeof(*xig)) {
			free(pbuf);
			return -1;
		}
		len += len / 8 + sizeof(*xig);
		free(pbuf);
		pbuf = malloc(len);
		if (!pbuf)
			return -1;
		if (sysctlbyname(sysctl_name, pbuf, &len, NULL, 0) == 0)
			break;
		if (errno != ENOMEM) {
			free(pbuf);
			return -1;
		}
	}
	if (attempt >= 3) {
		free(pbuf);
		return -1;
	}
	if (len < sizeof(*xig)) {
		free(pbuf);
		return -1;
	}
	xig = (struct xinpgen *)(void *)pbuf;
	*count_out = (uint64_t)xig->xig_count;
	free(pbuf);
	return 0;
}

static int sysctl_read_u64_flexible(const char *name, uint64_t *out)
{
	size_t sz = 0;

	*out = 0;
	if (sysctlbyname(name, NULL, &sz, NULL, 0) != 0 || !sz)
		return -1;
	if (sz == sizeof(uint64_t)) {
		return sysctlbyname(name, out, &sz, NULL, 0);
	}
	if (sz == sizeof(uint32_t)) {
		uint32_t v32 = 0;
		sz = sizeof(v32);
		if (sysctlbyname(name, &v32, &sz, NULL, 0) != 0)
			return -1;
		*out = v32;
		return 0;
	}
	if (sz == sizeof(int)) {
		int v = 0;
		sz = sizeof(v);
		if (sysctlbyname(name, &v, &sz, NULL, 0) != 0 || v < 0)
			return -1;
		*out = (uint64_t)v;
		return 0;
	}
	return -1;
}

void get_sockstat_stats(void)
{
	uint64_t used = 0;
	size_t sz;
	int tcp_from_states = 0;

	if (sysctl_read_u64_flexible("kern.ipc.numopensockets", &used) == 0)
		metric_add_auto("sockstat_sockets_used", &used, DATATYPE_UINT, ac->system_carg);

	/*
	 * net.inet.tcp.states: SYSCTL_COUNTER_U64_ARRAY of TCP_NSTATES
	 * (FreeBSD 11+). Element width is uint64_t, not u_long.
	 */
	{
		uint64_t states[TCP_NSTATES];
		sz = sizeof(states);
		if (sysctlbyname("net.inet.tcp.states", states, &sz, NULL, 0) == 0 &&
			sz >= sizeof(uint64_t) && (sz % sizeof(uint64_t)) == 0) {
			uint64_t inuse = 0;
			uint64_t tw = 0;
			int i;
			int n = (int)(sz / sizeof(uint64_t));

			if (n > TCP_NSTATES)
				n = TCP_NSTATES;
			for (i = 0; i < n; i++)
				inuse += states[i];
			if (TCPS_TIME_WAIT < n)
				tw = states[TCPS_TIME_WAIT];
			sockstat_emit_u64("TCP", "inuse", inuse);
			sockstat_emit_u64("TCP", "tw", tw);
			tcp_from_states = 1;
		}
	}

	if (!tcp_from_states) {
		uint64_t tcp_n = 0;
		if (sockstat_pcblist_count("net.inet.tcp.pcblist", &tcp_n) == 0)
			sockstat_emit_u64("TCP", "inuse", tcp_n);
	}

	{
		uint64_t udp_n = 0;
		if (sockstat_pcblist_count("net.inet.udp.pcblist", &udp_n) == 0)
			sockstat_emit_u64("UDP", "inuse", udp_n);
	}

	{
		uint64_t maxsock = 0;

		if (sysctl_read_u64_flexible("kern.ipc.maxsockets", &maxsock) == 0)
			sockstat_emit_u64("sockets", "max", maxsock);
	}
}
#endif
