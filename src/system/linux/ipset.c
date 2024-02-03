#ifdef __linux__
#include <errno.h>
#include <stdio.h>
#include <memory.h>
#include <stdlib.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <linux/netfilter/nfnetlink.h>
#include <netlink/attr.h>
#include <linux/netfilter/ipset/ip_set.h>
#include <inttypes.h>
#include <byteswap.h>
#include "dstructures/ht.h"
#include "metric/namespace.h"
#include "main.h"
#define IPSET_CMD_LIST NLM_F_MULTI|NLM_F_ACK|NLM_F_DUMP

void ipset_entry_data(char *kdt, uint16_t dsize, char *name)
{
	uint64_t okval = 1;
	char kvalue[255] = { 0 };
	char attrcomment[255] = { 0 };
	char cidr[3] = { 0 };
	char ipaddr[92] = { 0 };
	char ipaddr_to[46] = { 0 };
	char iface[255] = { 0 };
	char macaddr[19] = { 0 };
	uint64_t packets = 0;
	uint64_t timeout = 0;
	uint64_t mark = 0;
	uint64_t bytes = 0;
	uint16_t port = 0;
	uint8_t proto = 0;
	for (uint64_t k = 4; (k + 4) < dsize;)
	{
		uint16_t ksize = ((uint16_t)*(kdt + k));
		uint16_t ktype = ((uint16_t)*(kdt + k + 2));
		if (ac->system_carg->log_level > 0)
			printf("\t\t\t3[%"PRIu64"/%u] internal name: %s, type: %d/%d, size: %d: %u %u %u %u\n", k, dsize - 4, name, ktype, IPSET_ATTR_COMMENT, ksize, kdt[k+4], kdt[k+5], kdt[k+6], kdt[k+7]);

		memcpy(kvalue, kdt + k + 4, ksize - 4);
		if (ktype == IPSET_ATTR_HASHSIZE && ksize == 8)
		{
			uint32_t *kdvalue = (uint32_t*)kvalue;
			uint64_t uvalue = bswap_32(*kdvalue);
			metric_add_labels("ipset_hashsize", &uvalue, DATATYPE_UINT, ac->system_carg, "name", name);
		}
		else if (ktype == IPSET_ATTR_MAXELEM && ksize == 8)
		{
			uint32_t *kdvalue = (uint32_t*)kvalue;
			uint64_t uvalue = bswap_32(*kdvalue);
			metric_add_labels("ipset_maxelem", &uvalue, DATATYPE_UINT, ac->system_carg, "name", name);
		}
		else if (ktype == IPSET_ATTR_TIMEOUT && ksize == 8)
		{
			uint32_t *kdvalue = (uint32_t*)kvalue;
			timeout = bswap_32(*kdvalue);
			metric_add_labels("ipset_timeout", &timeout, DATATYPE_UINT, ac->system_carg, "name", name);
		}
		else if (ktype == IPSET_ATTR_ELEMENTS && ksize == 8)
		{
			uint32_t *kdvalue = (uint32_t*)kvalue;
			uint64_t uvalue = bswap_32(*kdvalue);
			metric_add_labels("ipset_entries_count", &uvalue, DATATYPE_UINT, ac->system_carg, "name", name);
		}
		else if (ktype == IPSET_ATTR_REFERENCES && ksize == 8)
		{
			uint32_t *kdvalue = (uint32_t*)kvalue;
			uint64_t uvalue = bswap_32(*kdvalue);
			metric_add_labels("ipset_references", &uvalue, DATATYPE_UINT, ac->system_carg, "name", name);
		}
		else if (ktype == IPSET_ATTR_MEMSIZE && ksize == 8)
		{
			uint32_t *kdvalue = (uint32_t*)kvalue;
			uint64_t uvalue = bswap_32(*kdvalue);
			metric_add_labels("ipset_memsize", &uvalue, DATATYPE_UINT, ac->system_carg, "name", name);
		}
		else if (!ac->system_ipset_entries)
		{
			k += NLMSG_ALIGN(ksize);
			continue;
		}
		else if (ktype == IPSET_ATTR_MARK)
		{
			uint32_t *kdvalue = (uint32_t*)kvalue;
			mark = bswap_32(*kdvalue);
		}
		else if (ktype == IPSET_ATTR_BYTES)
		{
			uint64_t *kdvalue = (uint64_t*)kvalue;
			bytes = bswap_64(*kdvalue);
		}
		else if (ktype == IPSET_ATTR_PACKETS)
		{
			uint64_t *kdvalue = (uint64_t*)kvalue;
			packets = bswap_64(*kdvalue);
		}
		else if (ktype == IPSET_ATTR_ETHER)
		{
			snprintf(macaddr, 18, "%.2X:%.2X:%.2X:%.2X:%.2X:%.2X", kvalue[0], kvalue[1], kvalue[2], kvalue[3], kvalue[4], kvalue[5]);
		}
		else if (ktype == IPSET_ATTR_COMMENT)
		{
			strlcpy(attrcomment, kvalue, 255);
		}
		else if (ktype == IPSET_ATTR_IFACE)
		{
			strlcpy(iface, kvalue, 255);
		}
		else if (ktype == IPSET_ATTR_IP)
		{
			if (ksize == 12) // ipv4
				snprintf(ipaddr, 40, "%hhu.%hhu.%hhu.%hhu", kvalue[4], kvalue[5], kvalue[6], kvalue[7]);
			else //ipv6
			{
				char hextet[5];
				for (uint64_t h = 4; h < ksize; h++)
				{
					snprintf(hextet, 4, "%X", kvalue[4]);
					if (*ipaddr)
						strcat(ipaddr, ":");
					strcat(ipaddr, hextet);
				}
			}
		}
		else if (ktype == IPSET_ATTR_CIDR)
		{
			snprintf(cidr, 3, "%"PRIu8, *kvalue);
		}
		else if (ktype == IPSET_ATTR_IP_TO)
		{
			if (ksize == 12) // ipv4
				snprintf(ipaddr_to, 40, "%hhu.%hhu.%hhu.%hhu", kvalue[4], kvalue[5], kvalue[6], kvalue[7]);
			else //ipv6
			{
				char hextet[5];
				for (uint64_t h = 4; h < ksize; h++)
				{
					snprintf(hextet, 4, "%X", kvalue[4]);
					if (*ipaddr_to)
						strcat(ipaddr_to, ":");
					strcat(ipaddr_to, hextet);
				}
			}
		}
		//else if (ktype == IPSET_ATTR_CIDR2) IPSET_ATTR_IP2
		else if (ktype == IPSET_ATTR_PROTO)
			proto = kvalue[0];
		else if (ktype == IPSET_ATTR_PORT)
			port = kvalue[1] + (kvalue[0] << 8);
		else if (ktype == IPSET_ATTR_ADT)
		{
		}
		else
		{
			if (ac->system_carg->log_level > 0)
				fprintf(stderr, "\t\tipset_data_entry unknown ipset attribute from kernel: %s: %u\n", name, ktype);
		}

		k += NLMSG_ALIGN(ksize);
	}

	if (!ac->system_ipset_entries)
		return;

	char *strproto = "";
	strproto = (proto == IPPROTO_TCP) ? "TCP" : strproto;
	strproto = (proto == IPPROTO_UDP) ? "UDP" : strproto;
	strproto = (proto == IPPROTO_ICMP) ? "ICMP" : strproto;
	strproto = (proto == IPPROTO_RAW) ? "RAW" : strproto;
	strproto = (proto == IPPROTO_SCTP) ? "SCTP" : strproto;

	char strport[6] = { 0 };
	if (port)
		snprintf(strport, 6, "%"PRIu16, port);

	char strmark[20] = { 0 };
	if (mark)
		snprintf(strmark, 19, "%"PRIu64, mark);

	if (*ipaddr || *cidr || *attrcomment || *iface || *macaddr || *strproto || *strport || *strmark)
	{
		if (ac->system_carg->log_level > 0)
			printf("\n\t\t\tip is %s, cidr is %s, range to %s, comment is %s, iface is %s, macaddr is %s, timeout is %"PRIu64", mark is %"PRIu64", proto is %s, bytes is %"PRIu64", packets is %"PRIu64", port is %"PRIu16"\n", ipaddr, cidr, ipaddr_to, attrcomment, iface, macaddr, timeout, mark, strproto, bytes, packets, port);
		if (*ipaddr_to)
			metric_add_labels10("ipset_entry_data", &okval, DATATYPE_UINT, ac->system_carg, "name", name, "addr", ipaddr, "prefix", cidr, "comment", attrcomment, "iface", iface, "mac", macaddr, "proto", strproto, "port", strport, "mark", strmark, "range_to", ipaddr_to);
		else
			metric_add_labels9("ipset_entry_data", &okval, DATATYPE_UINT, ac->system_carg, "name", name, "addr", ipaddr, "prefix", cidr, "comment", attrcomment, "iface", iface, "mac", macaddr, "proto", strproto, "port", strport, "mark", strmark);
	}
	if (timeout)
		metric_add_labels9("ipset_entry_timeout", &timeout, DATATYPE_UINT, ac->system_carg, "name", name, "addr", ipaddr, "prefix", cidr, "comment", attrcomment, "iface", iface, "mac", macaddr, "proto", strproto, "port", strport, "mark", strmark);
	if (bytes)
		metric_add_labels9("ipset_entry_bytes", &bytes, DATATYPE_UINT, ac->system_carg, "name", name, "addr", ipaddr, "prefix", cidr, "comment", attrcomment, "iface", iface, "mac", macaddr, "proto", strproto, "port", strport, "mark", strmark);
	if (packets)
		metric_add_labels9("ipset_entry_packets", &packets, DATATYPE_UINT, ac->system_carg, "name", name, "addr", ipaddr, "prefix", cidr, "comment", attrcomment, "iface", iface, "mac", macaddr, "proto", strproto, "port", strport, "mark", strmark);
}


void ipset()
{
	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_NETFILTER);
	if (fd < 0)
		return;

	struct sockaddr_nl nladdr;
	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;

	struct {
		struct nlmsghdr nlh; // 16
		struct nfgenmsg nfg; // 4
		struct nlattr nlattr; // 4
		uint32_t data; // 4
	} req;

	req.nlh.nlmsg_len = sizeof(req);
	req.nlh.nlmsg_type = htons((uint16_t)((IPSET_CMD_LIST) | (NFNL_SUBSYS_IPSET << 8)));
	req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_ROOT | NLM_F_MATCH | NLM_F_DUMP;
	req.nlh.nlmsg_pid = 0;
	req.nlh.nlmsg_seq = 1;

	req.nfg.nfgen_family = AF_INET;
	req.nfg.version = NFNETLINK_V0;
	req.nfg.res_id = 0;

	req.nlattr.nla_len = 0x05;
	req.nlattr.nla_type = 1;
	req.data = 6;

	struct iovec iov[3];
	iov[0] = (struct iovec) {
		.iov_base = &req,
		.iov_len = sizeof(req)
	};


	struct msghdr msg;

	msg = (struct msghdr) {
		.msg_name = (void*)&nladdr,
		.msg_namelen = sizeof(nladdr),
		.msg_iov = iov,
		.msg_iovlen = 1
	};

	int rep = sendmsg(fd, &msg, 0);
	if (rep < 0)
	{
		if (ac->system_carg->log_level > 1)
			printf("sendmsg error: ipset()\n");
		close(fd);
		return;
	}

	char fbuf[getpagesize() * 8];
	memset(fbuf, 0, getpagesize() * 8);
	while (rep > 0)
	{
		int status;
		struct nlmsghdr *h;
		
		iov[0] = (struct iovec) {
			.iov_base = fbuf,
			.iov_len = sizeof(fbuf)
		};

		msg = (struct msghdr) {
			(void*)&nladdr, sizeof(nladdr),
			iov, 1,
			NULL, 0,
			0
		};
		status = recvmsg(fd, &msg, 0);
		if (status < 0) {
			if (errno == EINTR)
				continue;
			printf("OVERRUN\n");
				continue;
		}
		if (status == 0)
			break;

		h = (struct nlmsghdr*)fbuf;
		uint64_t okval = 1;

		while (status > 0 && NLMSG_OK(h, (uint32_t) status))
		{
			char *dt = (char*)NLMSG_DATA(h);

			if (h->nlmsg_seq != 1) {
				h = NLMSG_NEXT(h, status);
			}

			if (h->nlmsg_type == NLMSG_DONE || h->nlmsg_type == NLMSG_ERROR)
			{
				status = -1;
				close(fd);
				return;
			}

			uint64_t dt_size = h->nlmsg_len;

			char value[255];
			char name[255] = { 0 };
			char typename[255] = { 0 };
			for (uint64_t i = 4; (i + 4) < dt_size;)
			{
				uint16_t size = (uint8_t)(dt[i + 1] << 8) + (uint8_t)(dt[i]);
				uint16_t type = ((uint16_t)*(dt + i + 2));
				if (ac->system_carg->log_level > 0)
					printf("\t1[%"PRIu64"/%"PRIu64"] name: %s, type: %d, size: %d: %hhu %hhu %hhu %hhu %hhu %hhu %hhu %hhu\n", i, dt_size, name, type, size, dt[i], dt[i+1], dt[i+2], dt[i+3], dt[i+4], dt[i+5], dt[i+6], dt[i+7]);

				if (size < 4)
				{
					if (ac->system_carg->log_level > 1)
						printf("ipset attribute 0x%02x has invalid length of %d bytes\n", type, size);
					break;
				}

				if (dt_size < (i+size))
				{
					if (ac->system_carg->log_level > 1)
						printf("ipset attribute 0x%02x of length %d is truncated, only %"PRIu64" bytes remaining\n", type, size, dt_size-i);
					break;
				}
				
				memcpy(value, dt + i + 4, size - 3);
				if (type == IPSET_ATTR_PROTOCOL)
				{
					//metric_add("ipset_attr_protocol", lbl, &vl, DATATYPE_UINT, ac->system_carg);
					//metric_add_labels("ipset_attr_protocol", &vl, DATATYPE_UINT, ac->system_carg, "name" );
					//printf("\tprotocol: %u\n", *value);
				}
				else if (type == IPSET_ATTR_SETNAME)
				{
					strlcpy(name, value, size - 3);
				}
				else if (type == IPSET_ATTR_COMMENT)
				{
				}
				else if (type == IPSET_ATTR_TYPENAME)
				{
					strlcpy(typename, value, size - 3);
					if (size > 5)
						metric_add_labels2("ipset_typename", &okval, DATATYPE_UINT, ac->system_carg, "name", name, "typename", typename);
				}
				else if (type == IPSET_ATTR_REVISION)
				{
					uint64_t revision = *value;
					metric_add_labels("ipset_revision", &revision, DATATYPE_UINT, ac->system_carg, "name", name);
				}
				else if (type == IPSET_ATTR_FAMILY)
				{
				}
				else if (type == IPSET_ATTR_FLAGS)
				{
				}
				else if (type == IPSET_ATTR_PROTOCOL_MIN)
				{
				}
				else if (type == IPSET_ATTR_MARKMASK)
				{
				}
				else if (type == IPSET_ATTR_DATA)
				{
					char *ddt = dt + i;
					ipset_entry_data(ddt, size, name);
				}
				else if (type == IPSET_ATTR_ADT)
				{
					char dvalue[255];
					char *ddt = dt + i;
					for (uint64_t j = 4; (j + 4) < size;)
					{
						uint16_t dsize = ((uint16_t)*(ddt + j));
						uint16_t dtype = ((uint16_t)*(ddt + j + 2));
						if (ac->system_carg->log_level > 0)
							printf("\t\t2[%"PRIu64"/%u] ADT type: '%s' %d, size: %d: %u %u %u %u\n", j, size - 4, typename, dtype, dsize, ddt[j+4], ddt[j+5], ddt[j+6], ddt[j+7]);

						memcpy(dvalue, ddt + j + 4, dsize - 4);
						if (dtype == IPSET_ATTR_DATA)
						{
							char *kdt = ddt + j;
							ipset_entry_data(kdt, dsize, name);
						}

						j += NLMSG_ALIGN(dsize);
					}
				}
				else
				{
					if (ac->system_carg->log_level > 0)
						fprintf(stderr, "\tunknown ipset attribute from kernel: %u, typename '%s'\n", type, typename);
				}

				i += NLMSG_ALIGN(size);
			}

			h = NLMSG_NEXT(h, status);
		}
	}
	close(fd);
}
#endif
