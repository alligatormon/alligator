#include <getopt.h>
#include <sys/errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include "libiptc/libiptc.h"
#include "libiptc/libip6tc.h"
#include "linux/netfilter/xt_limit.h"
#include "xtables.h"
#include "linux/netfilter/xt_set.h"
#include "linux/netfilter/xt_physdev.h"
#include "main.h"
#define IPTC_HANDLE	struct iptc_handle*

uint8_t alog2(int64_t x)
{
	unsigned int ans = 0;
	while (x >>= 1) ans++;
	return ans;
}

static int get_version(unsigned *version)
{
	int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
	struct ip_set_req_version req_version;
	socklen_t size = sizeof(req_version);
	
	req_version.op = IP_SET_OP_VERSION;
	getsockopt(sockfd, SOL_IP, SO_IP_SET, &req_version, &size);

	*version = req_version.version;
	
	return sockfd;
}

static void get_set_byid(char *setname, ip_set_id_t idx)
{
	struct ip_set_req_get_set req;
	socklen_t size = sizeof(struct ip_set_req_get_set);
	int sockfd;

	sockfd = get_version(&req.version);
	req.op = IP_SET_OP_GET_BYINDEX;
	req.set.index = idx;
	getsockopt(sockfd, SOL_IP, SO_IP_SET, &req, &size);
	close(sockfd);

	strncpy(setname, req.set.name, IPSET_MAXNAMELEN);
}

void get_iptables_info(const char *tablename, context_arg *system_carg)
{
	extern aconf *ac;

	IPTC_HANDLE h;
	char *chain = NULL;
	char *policy = NULL;
	struct xt_counters *counters;
	struct xt_counters chaincnt;
	struct ipt_tcp * tcpinfo;
	struct xt_rateinfo *rateinfo;
	struct ipt_entry_match *matchp;

	h = (void*)iptc_init(tablename);
	if ( !h )
	{
		printf("IPTC initializing: %s\n", iptc_strerror(errno));
		return;
	}

	for (chain = (char*)iptc_first_chain(h); chain; chain = (char*)iptc_next_chain(h))
	{
		policy = (char*)iptc_get_policy(chain, &chaincnt, h);

		if (ac->log_level > 3)
			printf("%-10s %-10s [%llu:%llu]\n", chain, policy, chaincnt.pcnt, chaincnt.bcnt);
		if (policy)
		{
			metric_add_labels3("firewall_packages", &chaincnt.pcnt, DATATYPE_UINT, system_carg, "fw", "netfilter", "chain", chain, "policy", policy);
			metric_add_labels3("firewall_bytes", &chaincnt.bcnt, DATATYPE_UINT, system_carg, "fw", "netfilter", "chain", chain, "policy", policy);
		}

		int64_t i = 1;
		const struct ipt_entry *en = NULL;
		for (en = iptc_first_rule(chain, h); en; en = iptc_next_rule(en, h), i++)
		{
			tommy_hashdyn *lbl = malloc(sizeof(*lbl));
			tommy_hashdyn_init(lbl);
			char str[INET_ADDRSTRLEN];
			uint8_t lg;

			inet_ntop(AF_INET, &en->ip.src.s_addr, str, INET_ADDRSTRLEN);
			if (ac->log_level > 3)
				printf("src %s, ", str);
			labels_hash_insert_nocache(lbl, "src", str);

			inet_ntop(AF_INET, &en->ip.smsk.s_addr, str, INET_ADDRSTRLEN);
			lg = alog2((int64_t)en->ip.smsk.s_addr+1);
			labels_hash_insert_nocache(lbl, "mask", str);
			if (ac->log_level > 3)
				printf("mask %s/%d, ", str, lg);

			inet_ntop(AF_INET, &en->ip.dst.s_addr, str, INET_ADDRSTRLEN);
			labels_hash_insert_nocache(lbl, "dst", str);
			if (ac->log_level > 3)
				printf("dst %s, ", str);

			inet_ntop(AF_INET, &en->ip.dmsk.s_addr, str, INET_ADDRSTRLEN);
			lg = alog2((int64_t)en->ip.dmsk.s_addr+1);
			if (ac->log_level > 3)
				printf("mask %s, /%d, ", str, lg);
			labels_hash_insert_nocache(lbl, "mask", str);

			if (en->ip.proto == 0)
			{
				labels_hash_insert_nocache(lbl, "proto", "all");
				if (ac->log_level > 3)
					printf("proto all, ");
			}
			else if (en->ip.proto == 6)
			{
				labels_hash_insert_nocache(lbl, "proto", "tcp");
				if (ac->log_level > 3)
					printf("proto tcp, ");
			}
			else if (en->ip.proto == 17)
			{
				labels_hash_insert_nocache(lbl, "proto", "udp");
				if (ac->log_level > 3)
					printf("proto udp, ");
			}
			else if (en->ip.proto == 1)
			{
				labels_hash_insert_nocache(lbl, "proto", "icmp");
				if (ac->log_level > 3)
					printf("proto icmp, ");
			}
			else if (en->ip.proto == 41)
			{
				labels_hash_insert_nocache(lbl, "proto", "ipv6");
				if (ac->log_level > 3)
					printf("proto ipv6, ");
			}

			size_t size = 0;
			size_t tosize = en->target_offset - sizeof(struct ipt_entry);
			for (matchp = (struct ipt_entry_match*)en->elems; matchp && tosize > size; )
			{
				if (!matchp->u.match_size || matchp->u.match_size > tosize)
					break;

				if (ac->log_level > 3)
					printf("'%s'(%d,%zu/%zu), ", matchp->u.user.name, matchp->u.match_size, size, tosize);

				if (!strncmp(matchp->u.user.name, "limit", 5))
				{
					rateinfo = (struct xt_rateinfo *)matchp->data;
					if (ac->log_level > 3)
						printf("limit: avg %"PRIu32", burst %"PRIu32", ", rateinfo->avg, rateinfo->burst);
				}
				else if (!strncmp(matchp->u.user.name, "tcp", 3))
				{
					tcpinfo = (struct ipt_tcp *)matchp->data;
					if (ac->log_level > 3)
						printf("spts %d:%d dpts %d:%d, ", tcpinfo->spts[0], tcpinfo->spts[1], tcpinfo->dpts[0], tcpinfo->dpts[1]);
				}
				else if (!strncmp(matchp->u.user.name, "physdev", 7))
				{
					struct xt_physdev_info * physdevinfo = (struct xt_physdev_info *)matchp->data;
					if (ac->log_level > 3)
						printf("dev %s, ", physdevinfo->physindev);
				}
				else if (!strncmp(matchp->u.user.name, "set", 3))
				{
					struct xt_set_info_match_v4 *match_info = (struct xt_set_info_match_v4 *)matchp->data;
					struct xt_set_info *info = &match_info->match_set;

					int64_t i;
					char setname[IPSET_MAXNAMELEN];

					get_set_byid(setname, info->index);
					if (ac->log_level > 3)
						printf("%s %s ", (info->flags & IPSET_INV_MATCH) ? " !" : "", setname); 
					for (i = 1; i <= info->dim; i++)
					{		
						//if (ac->log_level > 3)
							printf("%s%s ", i == 1 ? " " : ",", info->flags & (1 << i) ? "src" : "dst");
					}
				}
				else if (!strncmp(matchp->u.user.name, "comment", 7))
				{
					labels_hash_insert_nocache(lbl, "comment", (char*)matchp->data);
					if (ac->log_level > 3)
						printf("comment:%s, ", matchp->data);
				}
			
				size += matchp->u.match_size;
				matchp = (struct ipt_entry_match*)en->elems+size;
			}

			counters = iptc_read_counter(chain, i, h);
			char *target_name = (char*)iptc_get_target(en, h);
			if (ac->log_level > 3)
				printf("%-10s %-10s [%llu:%llu]\n", chain, target_name, counters->pcnt, counters->bcnt);
			labels_hash_insert_nocache(lbl, "target", target_name);
			labels_hash_insert_nocache(lbl, "chain", chain);
			tommy_hashdyn *byteslbl = labels_dup(lbl);
			metric_add("firewall_packages", lbl, &counters->pcnt, DATATYPE_UINT, system_carg);
			metric_add("firewall_bytes", byteslbl, &counters->bcnt, DATATYPE_UINT, system_carg);
		}
	}
	iptc_free(h);
}

void get_iptables6_info(const char *tablename, context_arg *system_carg)
{
	extern aconf *ac;

	IPTC_HANDLE h;
	char *chain = NULL;
	char *policy = NULL;
	struct xt_counters *counters;
	struct xt_counters chaincnt;
	struct ip6t_tcp * tcpinfo;
	struct xt_rateinfo *rateinfo;
	struct ip6t_entry_match *matchp;

	h = (void*)ip6tc_init(tablename);
	if ( !h )
	{
		printf("IPTC initializing: %s\n", ip6tc_strerror(errno));
		return;
	}

	for (chain = (char*)ip6tc_first_chain(h); chain; chain = (char*)ip6tc_next_chain(h))
	{
		policy = (char*)ip6tc_get_policy(chain, &chaincnt, h);

		if (ac->log_level > 3)
			printf("%-10s %-10s [%llu:%llu]\n", chain, policy, chaincnt.pcnt, chaincnt.bcnt);
		if (policy)
		{
			metric_add_labels3("firewall_packages", &chaincnt.pcnt, DATATYPE_UINT, system_carg, "fw", "netfilter", "chain", chain, "policy", policy);
			metric_add_labels3("firewall_bytes", &chaincnt.bcnt, DATATYPE_UINT, system_carg, "fw", "netfilter", "chain", chain, "policy", policy);
		}

		int64_t i = 1;
		const struct ip6t_entry *en = NULL;
		for (en = ip6tc_first_rule(chain, h); en; en = ip6tc_next_rule(en, h), i++)
		{
			tommy_hashdyn *lbl = malloc(sizeof(*lbl));
			tommy_hashdyn_init(lbl);
			//char str[INET_ADDRSTRLEN];
			//uint8_t lg;

			//inet_ntop(AF_INET, &en->ipv6.src.s_addr, str, INET_ADDRSTRLEN);
			//if (ac->log_level > 3)
			//	printf("src %s, ", str);
			//labels_hash_insert_nocache(lbl, "src", str);

			//inet_ntop(AF_INET, &en->ipv6.smsk.s_addr, str, INET_ADDRSTRLEN);
			//lg = alog2((int64_t)en->ipv6.smsk.s_addr+1);
			//labels_hash_insert_nocache(lbl, "mask", str);
			//if (ac->log_level > 3)
			//	printf("mask %s/%d, ", str, lg);

			//inet_ntop(AF_INET, &en->ipv6.dst.s_addr, str, INET_ADDRSTRLEN);
			//labels_hash_insert_nocache(lbl, "dst", str);
			//if (ac->log_level > 3)
			//	printf("dst %s, ", str);

			//inet_ntop(AF_INET, &en->ipv6.dmsk.s_addr, str, INET_ADDRSTRLEN);
			//lg = alog2((int64_t)en->ipv6.dmsk.s_addr+1);
			//if (ac->log_level > 3)
			//	printf("mask %s, /%d, ", str, lg);
			//labels_hash_insert_nocache(lbl, "mask", str);

			if (en->ipv6.proto == 0)
			{
				labels_hash_insert_nocache(lbl, "proto", "all");
				if (ac->log_level > 3)
					printf("proto all, ");
			}
			else if (en->ipv6.proto == 6)
			{
				labels_hash_insert_nocache(lbl, "proto", "tcp");
				if (ac->log_level > 3)
					printf("proto tcp, ");
			}
			else if (en->ipv6.proto == 17)
			{
				labels_hash_insert_nocache(lbl, "proto", "udp");
				if (ac->log_level > 3)
					printf("proto udp, ");
			}
			else if (en->ipv6.proto == 1)
			{
				labels_hash_insert_nocache(lbl, "proto", "icmp");
				if (ac->log_level > 3)
					printf("proto icmp, ");
			}
			else if (en->ipv6.proto == 41)
			{
				labels_hash_insert_nocache(lbl, "proto", "ipv6");
				if (ac->log_level > 3)
					printf("proto ipv6, ");
			}

			size_t size = 0;
			size_t tosize = en->target_offset - sizeof(struct ip6t_entry);
			for (matchp = (struct ip6t_entry_match*)en->elems; matchp && tosize > size; )
			{
				if (!matchp->u.match_size || matchp->u.match_size > tosize)
					break;

				if (ac->log_level > 3)
					printf("'%s'(%d,%zu/%zu), ", matchp->u.user.name, matchp->u.match_size, size, tosize);

				if (!strncmp(matchp->u.user.name, "limit", 5))
				{
					rateinfo = (struct xt_rateinfo *)matchp->data;
					if (ac->log_level > 3)
						printf("limit: avg %"PRIu32", burst %"PRIu32", ", rateinfo->avg, rateinfo->burst);
				}
				else if (!strncmp(matchp->u.user.name, "tcp", 3))
				{
					tcpinfo = (struct ip6t_tcp *)matchp->data;
					if (ac->log_level > 3)
						printf("spts %d:%d dpts %d:%d, ", tcpinfo->spts[0], tcpinfo->spts[1], tcpinfo->dpts[0], tcpinfo->dpts[1]);
				}
				else if (!strncmp(matchp->u.user.name, "physdev", 7))
				{
					struct xt_physdev_info * physdevinfo = (struct xt_physdev_info *)matchp->data;
					if (ac->log_level > 3)
						printf("dev %s, ", physdevinfo->physindev);
				}
				else if (!strncmp(matchp->u.user.name, "set", 3))
				{
					struct xt_set_info_match_v4 *match_info = (struct xt_set_info_match_v4 *)matchp->data;
					struct xt_set_info *info = &match_info->match_set;

					int64_t i;
					char setname[IPSET_MAXNAMELEN];

					get_set_byid(setname, info->index);
					if (ac->log_level > 3)
						printf("%s %s ", (info->flags & IPSET_INV_MATCH) ? " !" : "", setname); 
					for (i = 1; i <= info->dim; i++)
					{		
						//if (ac->log_level > 3)
							printf("%s%s ", i == 1 ? " " : ",", info->flags & (1 << i) ? "src" : "dst");
					}
				}
				else if (!strncmp(matchp->u.user.name, "comment", 7))
				{
					labels_hash_insert_nocache(lbl, "comment", (char*)matchp->data);
					if (ac->log_level > 3)
						printf("comment:%s, ", matchp->data);
				}
			
				size += matchp->u.match_size;
				matchp = (struct ip6t_entry_match*)en->elems+size;
			}

			counters = ip6tc_read_counter(chain, i, h);
			char *target_name = (char*)ip6tc_get_target(en, h);
			if (ac->log_level > 3)
				printf("%-10s %-10s [%llu:%llu]\n", chain, target_name, counters->pcnt, counters->bcnt);
			labels_hash_insert_nocache(lbl, "target", target_name);
			labels_hash_insert_nocache(lbl, "chain", chain);
			tommy_hashdyn *byteslbl = labels_dup(lbl);
			metric_add("firewall_packages", lbl, &counters->pcnt, DATATYPE_UINT, system_carg);
			metric_add("firewall_bytes", byteslbl, &counters->bcnt, DATATYPE_UINT, system_carg);
		}
	}
	ip6tc_free(h);
}
