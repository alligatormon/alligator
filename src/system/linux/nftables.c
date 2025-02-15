#ifdef __linux__
#include <linux/version.h>
# if LINUX_VERSION_CODE >= KERNEL_VERSION (4, 17, 0)
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <netinet/in.h>
#include <errno.h>
#include <byteswap.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <stddef.h>

#include <linux/netfilter.h>
#include <linux/netlink.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_log.h>
#include <linux/netfilter/nf_nat.h>

#ifndef NF_NAT_RANGE_NETMAP
#define NF_NAT_RANGE_NETMAP			(1 << 6)
#endif

#define ALLIGATOR_NFT_REJECT -10
#define ALLIGATOR_NFT_REDIRECT -11
#define ALLIGATOR_NFT_DUP -12
#define ALLIGATOR_NFT_MASQUERADE -13
#define ALLIGATOR_NFT_NAT -14

#define PAYLOAD_OFFSET_ADDRLEN 4
#define PAYLOAD_OFFSET_PORTSRC 0
#define PAYLOAD_OFFSET_PORTDST 2
#define PAYLOAD_OFFSET_ADDRSRC 12
#define PAYLOAD_OFFSET_ADDRDST 14

#define NLA_NESTED 8

#define CT_STATE_INVALID 1
#define CT_STATE_ESTABLISHED 2
#define CT_STATE_RELATED 4
#define CT_STATE_NEW 8
#define CT_STATE_UNTRACKED 64

#define SET_DATATYPE_VERDICT 1
#define SET_DATATYPE_NF_PROTO 2
#define SET_DATATYPE_BITMASK 3
#define SET_DATATYPE_INTEGER 4
#define SET_DATATYPE_STEING 5
#define SET_DATATYPE_LL_ADDR 6
#define SET_DATATYPE_IPV4 7
#define SET_DATATYPE_IPV6 8
#define SET_DATATYPE_ETHER_ADDR 9
#define SET_DATATYPE_ETHER_TYPE 10
#define SET_DATATYPE_INET_PROTO 12
#define SET_DATATYPE_INET_SERVICE 13
#define SET_DATATYPE_ICMP_TYPE 14
#define SET_DATATYPE_IFACE_TIME 18
#define SET_DATATYPE_IFACE_MARK 19
#define SET_DATATYPE_IFACE_INDEX 20
#define SET_DATATYPE_IFACE_TYPE 21
#define SET_DATATYPE_CT_STATE 26
#define SET_DATATYPE_CT_STATUS 28
#define SET_DATATYPE_ICMP_CODE 32
#define SET_DATATYPE_ICMPV6_CODE 33
#define SET_DATATYPE_ICMPX_CODE 34
#define SET_DATATYPE_BOOLEAN 39
#define SET_DATATYPE_IFNAME 41
#define SET_DATATYPE_IGMPTYPE 42


void nftables_send_query(int setfd, uint16_t nlmsg_type, int nlmsg_flags, uint32_t pid, uint32_t seq, uint8_t, void *body, size_t body_len, void *userdata);

size_t strlcpy(char *dst, const char *src, size_t dsize)
{
    const char *osrc = src;
    size_t nleft = dsize;

    if (nleft != 0) while (--nleft != 0) {
        if ((*dst++ = *src++) == '\0')
            break;
    }

    if (nleft == 0) {
        if (dsize != 0) *dst = '\0';
        while (*src++) ;
    }

    return(src - osrc - 1);
}


typedef struct nftable_struct
{
	struct nlattr nlattr;
	char data[];
} nftable_struct;

typedef struct nft_genmsg
{
	struct nfgenmsg nfgenmsg;
	char data[];
} nft_genmsg;

typedef struct nft_expression
{
	uint16_t len;
	uint16_t type;
	uint16_t namelen;
	uint16_t nametype;
	char data[];
} nft_expression;

typedef struct set_t {
	uint8_t allsets;
	uint16_t timeout;
	uint8_t is_constant;
	uint8_t is_anonymous;
	uint8_t is_interval;
	uint8_t is_map;
	uint8_t has_timeout;
	int16_t addr_size;
	int16_t port_size;
	int16_t other_size;
	uint32_t datatype;

	int8_t ct_state_related;
	int8_t ct_state_new;
	int8_t ct_state_invalid;
	int8_t ct_state_established;
	int8_t ct_state_untracked;
} set_t;

uint64_t to_uint64(char data[]) {
	char kvalue[8] = { 0 };
	memcpy(kvalue, data, 8);
	uint64_t *kdvalue = (uint64_t*)kvalue;
	return *kdvalue;
}

uint64_t to_uint64_swap(char data[]) {
	char kvalue[8] = { 0 };
	memcpy(kvalue, data, 8);
	uint64_t *kdvalue = (uint64_t*)kvalue;
	uint64_t swapvalue = bswap_64(*kdvalue);
	return swapvalue;
}

uint16_t to_uint16(char data[]) {
	char kvalue[2] = { 0 };
	memcpy(kvalue, data, 2);
	uint16_t *kdvalue = (uint16_t*)kvalue;
	return *kdvalue;
}

uint16_t to_uint16_swap(char data[]) {
	char kvalue[2] = { 0 };
	memcpy(kvalue, data, 2);
	uint16_t *kdvalue = (uint16_t*)kvalue;
	uint16_t swapvalue = bswap_16(*kdvalue);
	return swapvalue;
}

uint32_t to_uint32(char data[]) {
	char kvalue[4] = { 0 };
	memcpy(kvalue, data, 4);
	uint32_t *kdvalue = (uint32_t*)kvalue;
	return *kdvalue;
}

uint32_t to_uint32_swap(char data[]) {
	char kvalue[4] = { 0 };
	memcpy(kvalue, data, 4);
	uint32_t *kdvalue = (uint32_t*)kvalue;
	uint32_t swapvalue = bswap_32(*kdvalue);
	return swapvalue;
}

uint16_t nl_get_len(char data[]) {
	return to_uint16(data);
}
uint16_t nl_get_type(char data[]) {
	return to_uint16(data+2);
}

char* nl_get_data(char data[]) {
	return data+4;
}

char* toIpv4(char data[], int8_t prefix) {
	uint32_t kvalue = 0 ;
	memcpy(&kvalue, data, 4);
	struct in_addr ip_addr;
	ip_addr.s_addr = kvalue;
	if (prefix > -1) {
		char *ret = calloc(1, 21);
		snprintf(ret, 20, "%s/%"PRId8, inet_ntoa(ip_addr), prefix);
		return ret;
	} else {
		char *ret = strndup(inet_ntoa(ip_addr), 16);
		return ret;
	}
}

char *get_limit_units(uint64_t units_id) {
	switch (units_id) {
		case 1:
			return "sec";
		case 60:
			return "minute";
		case 3600:
			return "hour";
		case 86400:
			return "day";
		case 604800:
			return "week";
		default:
			return "";
	}
}

void dump_pkt(char *fbuf, int status) {
	struct nlmsghdr *h = (struct nlmsghdr*)fbuf;
	printf("DEBUG first len %u, status %d\n", h->nlmsg_len, status);
	for (uint64_t i = 0; i < status; ++i) {
		printf("DEBUG [%ld]: [%hhu]", i, fbuf[i]);
		if (isprint(fbuf[i]))
			printf("\t'%c'\n", fbuf[i]);
		else
			puts("");
	}
}

typedef struct nft_str_expression {
	char expression[12][255];
	uint64_t packets;
	uint64_t bytes;
	uint8_t len;
	uint16_t syms;
} nft_str_expression;

char *nft_get_verdict_by_id(int8_t verdict) {
	if (verdict == NF_DROP) {
		return "drop";
	} else if (verdict == NF_ACCEPT) {
		return "accept";
	} else if (verdict == NF_STOLEN) {
		return "stolen";
	} else if (verdict == NF_QUEUE) {
		return "queue";
	} else if (verdict == NF_REPEAT) {
		return "repeat";
	} else if (verdict == NF_STOP) {
		return "stop";
	} else if (verdict == NFT_RETURN) {
		return "return";
	} else if (verdict == NFT_GOTO) {
		return "goto";
	} else if (verdict == NFT_JUMP) {
		return "jump";
	} else if (verdict == NFT_BREAK) {
		return "break";
	} else if (verdict == NFT_CONTINUE) {
		return "continue";
	} else if (verdict == ALLIGATOR_NFT_REJECT) {
		return "reject";
	} else if (verdict == ALLIGATOR_NFT_REDIRECT) {
		return "redirect";
	} else if (verdict == ALLIGATOR_NFT_DUP) {
		return "dup";
	} else if (verdict == ALLIGATOR_NFT_MASQUERADE) {
		return "masquerade";
	} else if (verdict == ALLIGATOR_NFT_NAT) {
		return "NAT";
	} else {
		return "";
	}
}

void nft_add_expr(nft_str_expression *expression, uint8_t meta, int8_t cmpop, int8_t payload_offset, int8_t payload_base, char *lookup, int16_t addrsize, int16_t portsize, int16_t othersize, char *target, char *immediate_addr1, char *immediate_addr2, int32_t immediate_port1, int32_t immediate_port2, int8_t ct_key, int8_t states_data, char *objref, char *proto, uint16_t port, int8_t reject_icmp_type, int8_t reject_icmp_code, char *goto_chain, int8_t ct_state_related, int8_t ct_state_new, int8_t  ct_state_invalid, int8_t ct_state_established, int8_t ct_state_untracked, char *log_prefix, int16_t log_group, int32_t log_snaplen, int16_t log_qtreshold, int32_t log_level, int32_t log_flags, int64_t limit_rate, char* limit_unit, int32_t limit_burst, int32_t limit_type, int32_t limit_flags, int32_t numgen_mod, int32_t numgen_type, int32_t nat_type, int32_t nat_family, int32_t nat_flags, int32_t connlimit, int32_t masq_flags, int32_t masq_to_ports) {
	snprintf(expression->expression[expression->len], 254, "[");
	if (meta) {
		strcat(expression->expression[expression->len], " meta: '");
		if (meta == NFT_META_IIFNAME)
			strcat(expression->expression[expression->len], "iifname");
		else if (meta == NFT_META_OIFNAME)
			strcat(expression->expression[expression->len], "oifname");
		else if (meta == NFT_META_NFPROTO)
			strcat(expression->expression[expression->len], "nfproto");
		else if (meta == NFT_META_L4PROTO)
			strcat(expression->expression[expression->len], "l4proto");
		strcat(expression->expression[expression->len], "'");
	}
	if (cmpop > -1) {
		strcat(expression->expression[expression->len], " cmp: '");

		if (cmpop == NFT_CMP_EQ) {
			strcat(expression->expression[expression->len], "==");
		} else if (cmpop == NFT_CMP_NEQ) {
			strcat(expression->expression[expression->len], "!=");
		} else if (cmpop == NFT_CMP_LT) {
			strcat(expression->expression[expression->len], "<");
		} else if (cmpop == NFT_CMP_LTE) {
			strcat(expression->expression[expression->len], "<=");
		} else if (cmpop == NFT_CMP_GT) {
			strcat(expression->expression[expression->len], ">");
		} else if (cmpop == NFT_CMP_GTE) {
			strcat(expression->expression[expression->len], ">=");
		}

		strcat(expression->expression[expression->len], "'");
	}
	if (payload_offset > -1) {
		strcat(expression->expression[expression->len], " payload offset: '");

		if (payload_offset == PAYLOAD_OFFSET_ADDRLEN) {
			strcat(expression->expression[expression->len], "addren");
		} else if (payload_offset == PAYLOAD_OFFSET_PORTSRC) {
			strcat(expression->expression[expression->len], "sport");
		} else if (payload_offset == PAYLOAD_OFFSET_PORTDST) {
			strcat(expression->expression[expression->len], "dport");
		} else if (payload_offset == PAYLOAD_OFFSET_ADDRSRC) {
			strcat(expression->expression[expression->len], "saddr");
		} else if (payload_offset == PAYLOAD_OFFSET_ADDRDST) {
			strcat(expression->expression[expression->len], "daddr");
		}

		strcat(expression->expression[expression->len], "'");
	}
	if (payload_base > -1) {
		strcat(expression->expression[expression->len], " payload base: '");
		if (payload_base == NFT_PAYLOAD_NETWORK_HEADER) {
			strcat(expression->expression[expression->len], "network");
		} else if (payload_base == NFT_PAYLOAD_TRANSPORT_HEADER) {
			strcat(expression->expression[expression->len], "transport");
		} else {
			printf("unknown payload base: %hhu\n", payload_base);
		}

		strcat(expression->expression[expression->len], "'");
	}
	if (*lookup) {
		strcat(expression->expression[expression->len], " lookup: '");
		strcat(expression->expression[expression->len], lookup);
		strcat(expression->expression[expression->len], "'");
	}
	if (addrsize > -1) {
		char setsize_str[10];
		snprintf(setsize_str, 9, "%"PRId16, addrsize);
		strcat(expression->expression[expression->len], " addrsize: '");
		strcat(expression->expression[expression->len], setsize_str);
		strcat(expression->expression[expression->len], "'");
	}
	if (portsize > -1) {
		char setsize_str[10];
		snprintf(setsize_str, 9, "%"PRId16, portsize);
		strcat(expression->expression[expression->len], " portize: '");
		strcat(expression->expression[expression->len], setsize_str);
		strcat(expression->expression[expression->len], "'");
	}
	if (othersize > -1) {
		char setsize_str[10];
		snprintf(setsize_str, 9, "%"PRId16, othersize);
		strcat(expression->expression[expression->len], " otherize: '");
		strcat(expression->expression[expression->len], setsize_str);
		strcat(expression->expression[expression->len], "'");
	}
	if (target) {
		strcat(expression->expression[expression->len], " target: '");
		strcat(expression->expression[expression->len], target);
		strcat(expression->expression[expression->len], "'");
	}
	if (immediate_addr1) {
		strcat(expression->expression[expression->len], " immediate_addr1: '");
		strcat(expression->expression[expression->len], immediate_addr1);
		strcat(expression->expression[expression->len], "'");
	}
	if (immediate_addr2) {
		strcat(expression->expression[expression->len], " immediate_addr2: '");
		strcat(expression->expression[expression->len], immediate_addr2);
		strcat(expression->expression[expression->len], "'");
	}
	if (immediate_port1 > -1) {
		char portnum[20];
		snprintf(portnum, 19, "%"PRId32, immediate_port1);
		strcat(expression->expression[expression->len], " immediate_port1: '");
		strcat(expression->expression[expression->len], portnum);
		strcat(expression->expression[expression->len], "'");
	}
	if (immediate_port2 > -1) {
		char portnum[20];
		snprintf(portnum, 19, "%"PRId32, immediate_port2);
		strcat(expression->expression[expression->len], " immediate_port2: '");
		strcat(expression->expression[expression->len], portnum);
		strcat(expression->expression[expression->len], "'");
	}
	if (ct_state_related > -1) {
		if (ct_state_related == 1) {
			strcat(expression->expression[expression->len], " related: 'accept'");
		} else if (ct_state_related == 0) {
			strcat(expression->expression[expression->len], " related: 'drop'");
		}
	}
	if (ct_state_new > -1) {
		if (ct_state_new == 1) {
			strcat(expression->expression[expression->len], " new: 'accept'");
		} else if (ct_state_new == 0) {
			strcat(expression->expression[expression->len], " new: 'drop'");
		}
	}
	if (ct_state_invalid > -1) {
		if (ct_state_invalid == 1) {
			strcat(expression->expression[expression->len], " invalid: 'accept'");
		} else if (ct_state_invalid == 0) {
			strcat(expression->expression[expression->len], " invalid: 'drop'");
		}
	}
	if (ct_state_established > -1) {
		if (ct_state_established == 1) {
			strcat(expression->expression[expression->len], " established: 'accept'");
		} else if (ct_state_established == 0) {
			strcat(expression->expression[expression->len], " established: 'drop'");
		}
	}
	if (ct_state_untracked > -1) {
		if (ct_state_untracked == 1) {
			strcat(expression->expression[expression->len], " untracked: 'accept'");
		} else if (ct_state_untracked == 0) {
			strcat(expression->expression[expression->len], " untracked: 'drop'");
		}
	}
	if (ct_key > -1) {
		strcat(expression->expression[expression->len], " ct key: '");
		if (ct_key == NFT_CT_STATE) {
			strcat(expression->expression[expression->len], "state");
		}
		strcat(expression->expression[expression->len], "'");
	}
	if (states_data > -1) {
		strcat(expression->expression[expression->len], " states: [");
		uint8_t opened = 0;
		if ((states_data & CT_STATE_INVALID) > 0) {
			strcat(expression->expression[expression->len], "RELATED");
			opened = 1;
		}
		if ((states_data & CT_STATE_ESTABLISHED) > 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "ESTABLISHED");
			opened = 1;
		}
		if ((states_data & CT_STATE_RELATED) > 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "RELATED");
			opened = 1;
		}
		if ((states_data & CT_STATE_NEW) > 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "NEW");
			opened = 1;
		}
		if ((states_data & CT_STATE_UNTRACKED) > 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "UNTRACKED");
			opened = 1;
		}
		strcat(expression->expression[expression->len], "]");
	}
	if (*objref) {
		strcat(expression->expression[expression->len], " objref: '");
		strcat(expression->expression[expression->len], objref);
		strcat(expression->expression[expression->len], "'");
	}
	if (proto) {
		strcat(expression->expression[expression->len], " proto: '");
		strcat(expression->expression[expression->len], proto);
		strcat(expression->expression[expression->len], "'");
	}
	if (port) {
		char portnum[10];
		snprintf(portnum, 9, "%"PRIu16, port);
		strcat(expression->expression[expression->len], " port: '");
		strcat(expression->expression[expression->len], portnum);
		strcat(expression->expression[expression->len], "'");
	}
	if (reject_icmp_code > -1) {
		char type_str[8];
		snprintf(type_str, 7, "%"PRId16, reject_icmp_type);
		strcat(expression->expression[expression->len], " reject icmp_type: '");
		strcat(expression->expression[expression->len], type_str);
		strcat(expression->expression[expression->len], "', icmp_code: '");
		if (reject_icmp_code == NFT_REJECT_ICMPX_NO_ROUTE) {
			strcat(expression->expression[expression->len], "no route");
		} else if (reject_icmp_code == NFT_REJECT_ICMPX_PORT_UNREACH) {
			strcat(expression->expression[expression->len], "port unreachable");
		} else if (reject_icmp_code == NFT_REJECT_ICMPX_HOST_UNREACH) {
			strcat(expression->expression[expression->len], "host unreachable");
		} else if (reject_icmp_code == NFT_REJECT_ICMPX_ADMIN_PROHIBITED) {
			strcat(expression->expression[expression->len], "admin prohibited");
		}
		strcat(expression->expression[expression->len], "'");
	}
	if (*goto_chain) {
		strcat(expression->expression[expression->len], " goto: '");
		strcat(expression->expression[expression->len], goto_chain);
		strcat(expression->expression[expression->len], "'");
	}
	if (*log_prefix) {
		strcat(expression->expression[expression->len], " log prefix: '");
		strcat(expression->expression[expression->len], log_prefix);
		strcat(expression->expression[expression->len], "'");
	}
	if (log_group > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId16, log_group);
		strcat(expression->expression[expression->len], " log group: '");
		strcat(expression->expression[expression->len], str);
		strcat(expression->expression[expression->len], "'");
	}
	if (log_snaplen > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId32, log_snaplen);
		strcat(expression->expression[expression->len], " log snaplen: '");
		strcat(expression->expression[expression->len], str);
		strcat(expression->expression[expression->len], "'");
	}
	if (log_qtreshold > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId32, log_qtreshold);
		strcat(expression->expression[expression->len], " log qtreshold: '");
		strcat(expression->expression[expression->len], str);
		strcat(expression->expression[expression->len], "'");
	}
	if (log_level > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId32, log_level);
		strcat(expression->expression[expression->len], " log_level: '");
		strcat(expression->expression[expression->len], str);
		strcat(expression->expression[expression->len], "'");
	}
	if (log_flags > -1) {
		strcat(expression->expression[expression->len], " log_flags: [");
		uint8_t opened = 0;
		if ((log_flags & NF_LOG_TCPSEQ) > 0) {
			strcat(expression->expression[expression->len], "'tcp seq'");
			opened = 1;
		}
		if ((log_flags & NF_LOG_TCPOPT) > 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "'tcp opt'");
			opened = 1;
		}
		if ((log_flags & NF_LOG_IPOPT) > 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "'ip opt'");
			opened = 1;
		}
		if ((log_flags & NF_LOG_UID) > 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "'uid'");
			opened = 1;
		}
		if ((log_flags & NF_LOG_NFLOG) > 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "'nflog'");
			opened = 1;
		}
		if ((log_flags & NF_LOG_MACDECODE) > 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "'macdecode'");
			opened = 1;
		}
		if ((log_flags & NF_LOG_MASK) > 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "'mask'");
			opened = 1;
		}
		strcat(expression->expression[expression->len], "]");
	}
	if (limit_rate > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId64, limit_rate);
		strcat(expression->expression[expression->len], " limit_rate: '");
		strcat(expression->expression[expression->len], str);
		strcat(expression->expression[expression->len], "'");
	}
	if (*limit_unit) {
		strcat(expression->expression[expression->len], " limit_unit: '");
		strcat(expression->expression[expression->len], limit_unit);
		strcat(expression->expression[expression->len], "'");
	}
	if (limit_burst > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId32, limit_burst);
		strcat(expression->expression[expression->len], " limit_burst: '");
		strcat(expression->expression[expression->len], str);
		strcat(expression->expression[expression->len], "'");
	}
	if (limit_type > -1) {
		if (limit_type == NFT_LIMIT_PKTS)
			strcat(expression->expression[expression->len], " limit_type: 'packets'");
		else if (limit_type == NFT_LIMIT_PKT_BYTES)
			strcat(expression->expression[expression->len], " limit_type: 'bytes'");
	}
	if (limit_flags > -1) {
		strcat(expression->expression[expression->len], " limit_flags: [");
		uint8_t opened = 0;
		if ((limit_flags & NFT_LIMIT_F_INV) > 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "'over'");
			opened = 1;
		}
	}
	if (numgen_mod > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId32, numgen_mod);
		strcat(expression->expression[expression->len], " numgen mod: '");
		strcat(expression->expression[expression->len], str);
		strcat(expression->expression[expression->len], "'");
	}

	if (numgen_type > -1) {
		strcat(expression->expression[expression->len], " numgen type: ");
		if ((numgen_type & NFT_NG_INCREMENTAL) > 0) {
			strcat(expression->expression[expression->len], "'round-robin'");
		} else if ((numgen_type & NFT_NG_RANDOM) > 0) {
			strcat(expression->expression[expression->len], "'random'");
		}
	}

	if (nat_type > -1) {
		strcat(expression->expression[expression->len], " nat type: ");
		if (nat_type == NFT_NAT_SNAT) {
			strcat(expression->expression[expression->len], "'snat'");
		} else if (nat_type == NFT_NAT_DNAT) {
			strcat(expression->expression[expression->len], "'dnat'");
		}
	}
	if (nat_flags > -1) {
		strcat(expression->expression[expression->len], " nat_flags: [");
		uint8_t opened = 0;
		if ((nat_flags & NF_NAT_RANGE_PERSISTENT) != 0) {
			strcat(expression->expression[expression->len], "'persistent'");
			opened = 1;
		}
		if ((nat_flags & NF_NAT_RANGE_PROTO_RANDOM) != 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "'random'");
			opened = 1;
		}
		if ((nat_flags & NF_NAT_RANGE_PROTO_RANDOM_FULLY) != 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "'random-fully'");
			opened = 1;
		}
		if ((nat_flags & NF_NAT_RANGE_NETMAP) != 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "'prefix'");
			opened = 1;
		}
		if ((nat_flags & NF_NAT_RANGE_PROTO_SPECIFIED) != 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "'range-proto-specified'");
			opened = 1;
		}
		strcat(expression->expression[expression->len], "]");
	}
	if (connlimit > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId32, connlimit);
		strcat(expression->expression[expression->len], " connlimit: '");
		strcat(expression->expression[expression->len], str);
		strcat(expression->expression[expression->len], "'");
	}
	if (masq_flags > -1) {
		strcat(expression->expression[expression->len], " masquerade_flags: [");
		uint8_t opened = 0;
		if ((masq_flags & NF_NAT_RANGE_PERSISTENT) != 0) {
			strcat(expression->expression[expression->len], "'persistent'");
			opened = 1;
		}
		if ((masq_flags & NF_NAT_RANGE_PROTO_RANDOM) != 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "'random'");
			opened = 1;
		}
		if ((masq_flags & NF_NAT_RANGE_PROTO_RANDOM_FULLY) != 0) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "'random-fully'");
			opened = 1;
		}
		if (masq_to_ports > -1) {
			if (opened)
				strcat(expression->expression[expression->len], ", ");
			strcat(expression->expression[expression->len], "'to-ports'");
			opened = 1;
		}
		strcat(expression->expression[expression->len], "]");
	}

	if (masq_to_ports > -1) {
		strcat(expression->expression[expression->len], " masquerade [to-ports]");
	}

	strcat(expression->expression[expression->len], "]");
	expression->syms += strlen(expression->expression[expression->len]);
	expression->len += 1;
}

char* nft_expression_deserialize(nft_str_expression* expression) {
	if (!expression)
		return NULL;

	char *exp = calloc(1, expression->syms + 1);
	for (uint8_t i = 0; i < expression->len; ++i) {
		strcat(exp, expression->expression[i]);
	}

	return exp;
}

nft_str_expression* parse_expressions(int fd, char *table, char *chain, char *data, uint64_t size, int8_t *verdict) {
	char *curdt;
	uint64_t i = 0;
	int8_t cmpop = -1;
	int meta = 0;
	int updated = 0;
	int prevcmp = 0;
	int counter_exists = 0;
	uint8_t ct_direction = 0;
	uint16_t port = 0;
	int8_t ct_key = -1; // NFT_CT_STATE
	int8_t states_data = -1;
	char *target = NULL;
	char *immediate_addr1 = NULL;
	char *immediate_addr2 = NULL;
	int32_t immediate_port1 = -1;
	int32_t immediate_port2 = -1;
	char objref[255] = { 0 };
	char lookup[255] = { 0 };
	char goto_chain[255] = { 0 };
	char log_prefix[255] = { 0 };
	int16_t log_group = -1;
	int32_t log_snaplen = -1;
	int16_t log_qtreshold = -1;
	int32_t log_level = -1;
	int32_t log_flags = -1;
	char *proto = NULL;
	int8_t reject_icmp_type = -1;
	int8_t reject_icmp_code = -1;
	*verdict = -127;
	nft_str_expression *expression = calloc(1, sizeof(*expression));
	int8_t payload_base = -1; //NFT_PAYLOAD_LL_HEADER, NFT_PAYLOAD_NETWORK_HEADER, NFT_PAYLOAD_TRANSPORT_HEADER, NFT_PAYLOAD_INNER_HEADER
	int8_t payload_offset = -1;
	int16_t addrsize = -1;
	int16_t portsize = -1;
	int16_t othersize = -1;
	int8_t ct_state_related = -1;
	int8_t ct_state_new = -1;
	int8_t ct_state_invalid= -1;
	int8_t ct_state_established = -1;
	int8_t ct_state_untracked = -1;
	int64_t limit_rate = -1;
	char* limit_unit = "";
	int32_t limit_burst = -1;
	int32_t limit_type = -1;
	int32_t limit_flags = -1;
	int32_t numgen_mod = -1;
	int32_t numgen_type = -1;
	int32_t nat_type = -1;
	int32_t nat_family = -1;
	int32_t nat_flags = -1;
	int32_t masq_to_ports = -1;
	int32_t masq_flags = -1;
	int32_t connlimit = -1;

	for (i = 0; i < size; ) {
		curdt = data + i;
		nft_expression *expr = (nft_expression *)curdt;
		if (!expr->len)
			break;

		if (expr->len + i > size)
			break;


		uint16_t nextlen = expr->data[expr->namelen - 1];

		int is_payload = 0;
		int is_cmp = 0;
		int is_lookup = 0;
		int is_immediate = 0;
		if (!strncmp(expr->data, "payload", 7)) {
			is_payload = 1;
		} else if (!strncmp(expr->data, "cmp", 3)) {
			is_cmp = 1;
		} else if (!strncmp(expr->data, "lookup", 6)) {
			is_lookup = 1;
		} else if (!strncmp(expr->data, "immediate", 9)) {
			is_immediate = 1;
		}


		if (prevcmp && !is_payload && !is_cmp && !is_lookup && !is_immediate) {
		        nft_add_expr(expression, meta, cmpop, payload_offset, payload_base, lookup, addrsize, portsize, othersize, target, immediate_addr1, immediate_addr2, immediate_port1, immediate_port2, ct_key, states_data, objref, proto, port, reject_icmp_type, reject_icmp_code, goto_chain, ct_state_related, ct_state_new, ct_state_invalid, ct_state_established, ct_state_untracked, log_prefix, log_group, log_snaplen, log_qtreshold, log_level, log_flags, limit_rate, limit_unit, limit_burst, limit_type, limit_flags, numgen_mod, numgen_type, nat_type, nat_family, nat_flags, connlimit, masq_flags, masq_to_ports);
		        prevcmp = 0;
		        updated = 1;
		        *lookup = 0;
		        *objref = 0;
		        *goto_chain = 0;
		        proto = NULL;
		        port = 0;
		        meta = 0;
		        cmpop = -1;
		        ct_key = -1;
			states_data = -1;
		        reject_icmp_type = -1;
		        reject_icmp_code = -1;
		        *verdict = -127;
		        payload_offset = -1;
		        payload_base = -1;
			portsize = -1;
			othersize = -1;
			addrsize = -1;
			ct_state_related = -1;
			ct_state_new = -1;
			ct_state_invalid= -1;
			ct_state_established = -1;
			ct_state_untracked = -1;
			*log_prefix = 0;
			log_group = -1;
			log_snaplen = -1;
			log_qtreshold = -1;
			log_level = -1;
			log_flags = -1;
			limit_rate = -1;
			limit_unit = "";
			limit_burst = -1;
			limit_type = -1;
			limit_flags = -1;
			numgen_mod = -1;
			numgen_type = -1;
			nat_type = -1;
			nat_family = -1;
			nat_flags = -1;
			masq_to_ports = -1;
			masq_flags = -1;
			connlimit = -1;
		        if (target)
		                free(target);
		        target = 0;
			if (!is_immediate) {
		        	if (immediate_addr1)
		        	        free(immediate_addr1);
		        	immediate_addr1 = 0;
		        	if (immediate_addr2)
		        	        free(immediate_addr2);
		        	immediate_addr2 = 0;
				immediate_port1 = -1;
				immediate_port2 = -1;
			}
                } else {
                       updated = 0;
                }

		// payload must be before cmp
		if (!strncmp(expr->data, "meta", 4)) {
			uint16_t meta_type = expr->data[expr->namelen + 10];
			meta = meta_type;
		} else if (is_payload) {
			meta = 0;
			uint16_t payload_size = expr->data[expr->namelen - 4];
			for (uint16_t j = expr->namelen; j < payload_size; ) {
				uint16_t plen = to_uint16(expr->data + j);
				if (!plen)
					break;
				uint16_t ptype = to_uint16(expr->data + j+2);
				if (ptype == NFTA_PAYLOAD_LEN) {
				} else if (ptype == NFTA_PAYLOAD_OFFSET) {
					payload_offset = expr->data[j+7];
				} else if (ptype == NFTA_PAYLOAD_BASE) {
					payload_base = expr->data[j+7];
				}


				j += NLA_ALIGN(plen);
			}
		} else if (is_cmp) {
			prevcmp = 1;
			for (uint64_t j = 0; j < expr->len; ) {
				uint16_t cmplen = expr->data[expr->namelen+j];
				uint16_t cmptype = expr->data[expr->namelen+j+2];
				if (cmptype == NFTA_CMP_OP) {
					uint8_t cmpdata = expr->data[expr->namelen+j+7];
					cmpop = cmpdata;
				} else if (cmptype == NFTA_CMP_DATA) {
					if (meta == NFT_META_IIFNAME) {
						target = strndup(expr->data + expr->namelen+j+8, 10);
					} else if (meta == NFT_META_NFPROTO) {
						int proto_id = expr->data[expr->namelen+j+8];
						if (proto_id == NFPROTO_IPV4)
							proto = "ipv4";
						else if (proto_id == NFPROTO_ARP)
							proto = "arp";
						else if (proto_id == NFPROTO_INET)
							proto = "inet";
						else if (proto_id == NFPROTO_NETDEV)
							proto = "netdev";
						else if (proto_id == NFPROTO_BRIDGE)
							proto = "bridge";
						else if (proto_id == NFPROTO_IPV6)
							proto = "ipv6";
						else if (proto_id == NFPROTO_DECNET)
							proto = "decnet";
					} else if (meta == NFT_META_L4PROTO) {
						int proto_id = expr->data[expr->namelen+j+8];
						if (proto_id == IPPROTO_UDP)
							proto = "udp";
						else if (proto_id == IPPROTO_TCP)
							proto = "tcp";
						else if (proto_id == IPPROTO_ICMP)
							proto = "icmp";
						else if (proto_id == IPPROTO_IGMP)
							proto = "igmp";
						else {
							fprintf(stderr, "unknown proto: %d\n", proto_id);
						}
					} else if (payload_base == NFT_PAYLOAD_NETWORK_HEADER) {
						payload_base = -1;
						int mask_bytes = to_uint16(expr->data + expr->namelen+j+4);
						int8_t prefix = (mask_bytes - 4) * 8;
						target = toIpv4(expr->data + expr->namelen+j+8, prefix);
					} else if (payload_base == NFT_PAYLOAD_TRANSPORT_HEADER) {
						if (proto && (!strcmp(proto, "tcp") || !strcmp(proto, "udp")))
							port = to_uint16_swap(expr->data + expr->namelen+j+8);
						payload_base = -1;
					} else if (proto) {
						port = to_uint16(expr->data + expr->namelen+j+8);
					}
					else {
						uint16_t k = expr->namelen+j;
						uint16_t len = nl_get_len(expr->data + k);
						uint16_t type = nl_get_type(expr->data + k);
						if ((type == 3) && (len == 12)) {
							target = toIpv4(expr->data + expr->namelen+j+8, -1);
						}
					}
				}

				j += NLA_ALIGN(cmplen);
			}
		} else if (is_lookup) {
			size_t lookup_len = strlcpy(lookup, expr->data + expr->namelen+5, 255) +1;

			size_t table_len = strlen(table) + 1;
            char buffer[4+32+4+32];
            memset(&buffer, 0, sizeof(buffer));
            struct nlattr *tnlattr = buffer;
			//struct {
			//	struct nlattr tnlattr;
			//	char table[NLA_ALIGN(table_len)];
			//	struct nlattr snlattr;
			//	char set[NLA_ALIGN(lookup_len)];
			//} body;
			//memset(&body, 0, sizeof(body));

			tnlattr.nla_len = table_len + sizeof(struct nlattr);
			tnlattr.nla_type = 0x1;
			memcpy(&buffer + sizeof(struct nlattr), table, table_len);

            char* ptr_to_set = buf + sizeof(struct nlattr) + NLA_ALIGN(table_len);
            struct nlattr *tnlattr = ptr_to_set;

			snlattr.nla_len = lookup_len + sizeof(struct nlattr);
			snlattr.nla_type = 0x2;

			memcpy(ptr_to_set, lookup, lookup_len);

            size_t buffer_size = sizeof(struct nlattr) * 2 + NLA_ALIGN(table_len) + NLA_ALIGN(lookup_len)


			set_t set = { 0 };

			set.ct_state_related = -1;
			set.ct_state_new = -1;
			set.ct_state_invalid = -1;
			set.ct_state_established = -1;
			set.ct_state_untracked = -1;

			nftables_send_query(0, (NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_GETSET, NLM_F_REQUEST|NLM_F_ACK, getpid(), 2, NFPROTO_INET, buffer, buffer_size, &set);
			addrsize = set.addr_size;
			portsize = set.port_size;
			othersize = set.other_size;
			ct_state_related = set.ct_state_related;
			ct_state_untracked = set.ct_state_untracked;
			ct_state_established = set.ct_state_established;
			ct_state_invalid = set.ct_state_invalid;
			ct_state_new = set.ct_state_new;
		} else if (!strncmp(expr->data, "ct", 2)) {
			uint16_t ct_size = expr->data[expr->namelen - 3];
			for (uint16_t j = expr->namelen+1; j < ct_size; ) {
				uint16_t ctlen = to_uint16(expr->data + j);
				if (!ctlen)
					break;
				uint16_t cttype = to_uint16(expr->data + j+2);
				if (cttype == NFTA_CT_KEY) {
					ct_key = expr->data[j+7];
				} else if (cttype == NFTA_CT_DIRECTION) {
					ct_direction = expr->data[j+7];
				}

				j += NLA_ALIGN(ctlen);
			}
		} else if (!strncmp(expr->data, "objref", 6)) {
			strlcpy(objref, expr->data + expr->namelen+5, 255);
		} else if (!strncmp(expr->data, "counter", 7)) {
			uint16_t counter_size = expr->data[expr->namelen - 4];
                        counter_exists = 1;
			for (uint16_t j = expr->namelen; j < counter_size; ) {
				uint16_t counter_len = nl_get_len(expr->data + j);
				if (!counter_len)
					break;
				uint16_t counter_type = nl_get_type(expr->data + j);
				char *data = nl_get_data(expr->data + j);
				if (counter_type == NFTA_COUNTER_BYTES) {
					expression->bytes = to_uint64_swap(data);
				} else if (counter_type == NFTA_COUNTER_PACKETS) {
					expression->packets = to_uint64_swap(data);
				}

				j += NLA_ALIGN(counter_len);
			}
		} else if (is_immediate) {
			uint8_t im_type = expr->data[14];
			if ((expr->len > 39) && (im_type == NFTA_DATA_VERDICT)) {
				*verdict = expr->data[39];
				// legacy
				if ((*verdict == NFT_GOTO) || (*verdict == NFT_JUMP)) {
					int goto_size = to_uint16(expr->data + 40);
					strlcpy(goto_chain, expr->data + 44, goto_size - 1);
					printf("goto chain is ");
					updated = 0;
				}
				//new
				else {
					for (uint32_t j = 16; j < expr->len - 15; ) {
						uint16_t type = nl_get_type(expr->data + j);
						uint16_t len = nl_get_len(expr->data + j);
						char *data = nl_get_data(expr->data + j);
						if (type == 2)
						{
							uint16_t k = j + 4;
							uint16_t len = nl_get_len(expr->data + k);
							char* ndata = nl_get_data(expr->data + k);
							if (len == 8) {
								if (!immediate_addr1)
									immediate_addr1 = toIpv4(ndata, -1);
								else if (!immediate_addr2)
									immediate_addr2 = toIpv4(ndata, -1);
							}
							if (len == 6) {
								if (immediate_port1 == -1)
									immediate_port1 = to_uint16_swap(ndata);
								else if (immediate_port2 == -1)
									immediate_port2 = to_uint16_swap(ndata);
							}
						}
						j += NLA_ALIGN(len);
					}
				}
			}
		} else if (!strncmp(expr->data, "reject", 6)) {
			*verdict = ALLIGATOR_NFT_REJECT;
			uint8_t im_code, im_type;
			uint16_t im_size = to_uint16(expr->data + expr->namelen -3) + expr->namelen;
			for (uint16_t j = expr->namelen+1; j < im_size; ) {
				uint16_t imlen = to_uint16(expr->data + j);
				if (!imlen)
					break;
				uint16_t rejecttype = to_uint16(expr->data + j+2);
				if (rejecttype == NFTA_REJECT_ICMP_CODE) {
					reject_icmp_code = expr->data[j+4];
				} else if (rejecttype == NFTA_REJECT_TYPE) {
					reject_icmp_type = expr->data[j+7];
				}

				j += NLA_ALIGN(imlen);
			}
			updated = 0;
		} else if (!strncmp(expr->data, "queue", 5)) {
			*verdict = NF_QUEUE;
			updated = 0;
		} else if (!strncmp(expr->data, "bitwise", 7)) {
			uint16_t bitwise_size = to_uint16(expr->data + 8);
			for (uint16_t j = 12; j < bitwise_size - 4; ) {
				uint16_t bitwise_len = to_uint16(expr->data + j);
				uint16_t bitwise_type = to_uint16(expr->data + j+2);
				uint16_t data_size = bitwise_len - 4;
				if (!bitwise_len)
					break;
				uint16_t bittype = to_uint16(expr->data + j+2);
				if (bittype == NFTA_BITWISE_LEN) {
				} else if (bittype == NFTA_BITWISE_MASK) {
					uint16_t k = j + 4;
					uint16_t type = nl_get_type(expr->data + k);
					uint16_t len = nl_get_len(expr->data + k);
					if (type == NFTA_DATA_VALUE)
					{
						uint16_t a = k + 4;
						if (len == 8) {
							states_data = to_uint16(expr->data + a);
						}
					}
				} else if (bittype == NFTA_BITWISE_XOR) {

				}

				j += NLA_ALIGN(bitwise_len);
			}
		} else if (!strncmp(expr->data, "log", 3)) {
			uint16_t log_size = to_uint16(expr->data + 4);
			for (uint16_t j = 8; j < expr->len - 11; ) {
				uint16_t type = nl_get_type(expr->data + j);
				uint16_t len = nl_get_len(expr->data + j);
				char *data = nl_get_data(expr->data + j);
				if (!len)
					break;

				if (type == NFTA_LOG_PREFIX) {
					strlcpy(log_prefix, data, 255);
				} else if (type == NFTA_LOG_GROUP) {
					log_group = to_uint16_swap(data);
				} else if (type == NFTA_LOG_SNAPLEN) {
					log_snaplen = to_uint32_swap(data);
				} else if (type == NFTA_LOG_QTHRESHOLD) {
					log_qtreshold = to_uint16_swap(data);
				} else if (type == NFTA_LOG_LEVEL) {
					log_level = to_uint32_swap(data);
				} else if (type == NFTA_LOG_FLAGS) {
					log_flags = to_uint32_swap(data);
				}
				j += NLA_ALIGN(len);
			}
		} else if (!strncmp(expr->data, "limit", 5)) {
			uint16_t limit_size = to_uint16(expr->data + 4);
			for (uint16_t j = 12; j < expr->len - 11; ) {
				uint16_t type = nl_get_type(expr->data + j);
				uint16_t len = nl_get_len(expr->data + j);
				char *data = nl_get_data(expr->data + j);
				if (!len)
					break;

				if (type == NFTA_LIMIT_RATE) {
					limit_rate = to_uint64_swap(data);
				} else if (type == NFTA_LIMIT_UNIT) {
					limit_unit = get_limit_units(to_uint64_swap(data));
				} else if (type == NFTA_LIMIT_BURST) {
					limit_burst = to_uint32_swap(data);
				} else if (type == NFTA_LIMIT_TYPE) {
					limit_type = to_uint32_swap(data);
				} else if (type == NFTA_LIMIT_FLAGS) {
					limit_flags = to_uint32_swap(data);
				}
				j += NLA_ALIGN(len);
			}
		} else if (!strncmp(expr->data, "nat", 3)) {
			*verdict = ALLIGATOR_NFT_NAT;
			uint16_t limit_size = to_uint16(expr->data + 4);
			uint16_t start_point = NLA_ALIGN(3 + 4);
			//dump_pkt(expr->data, expr->len - (start_point - 1));
			for (uint16_t j = start_point; j < expr->len - (start_point); ) {
				uint16_t type = nl_get_type(expr->data + j);
				uint16_t len = nl_get_len(expr->data + j);
				char *data = nl_get_data(expr->data + j);
				if (!len)
					break;

				if (type == NFTA_NAT_TYPE) {
					nat_type = to_uint32_swap(data);
				} else if (type == NFTA_NAT_FAMILY) {
					nat_family = to_uint32_swap(data);
				} else if (type == NFTA_NAT_FLAGS) {
					nat_flags = to_uint32_swap(data);
				}
				j += NLA_ALIGN(len);
			}
		} else if (!strncmp(expr->data, "numgen", 6)) {
			uint16_t limit_size = to_uint16(expr->data + 4);
			for (uint16_t j = 12; j < expr->len - 11; ) {
				uint16_t type = nl_get_type(expr->data + j);
				uint16_t len = nl_get_len(expr->data + j);
				char *data = nl_get_data(expr->data + j);
				if (!len)
					break;

				if (type == NFTA_NG_MODULUS) {
					numgen_mod = to_uint32_swap(data);
				} else if (type == NFTA_NG_TYPE) {
					numgen_type = to_uint32_swap(data);
				}
				j += NLA_ALIGN(len);
			}
		} else if (!strncmp(expr->data, "connlimit", 9)) {
			uint64_t j = NLA_ALIGN(10);
			uint16_t type = nl_get_type(expr->data + j);
			uint16_t len = nl_get_len(expr->data + j);

			for (uint64_t k = j + 4; k < expr->len;) {
				uint16_t nested_len = nl_get_len(expr->data + k);
				uint16_t nested_type = nl_get_type(expr->data + k);
				char *data = nl_get_data(expr->data + k);
				if ((nested_type == NFTA_CONNLIMIT_COUNT) && (nested_len == 8)) {
					connlimit = to_uint32_swap(data);
				}
				k += NLA_ALIGN(len);
			}
		} else if (!strncmp(expr->data, "masq", 4)) {
			*verdict = ALLIGATOR_NFT_MASQUERADE;
			uint64_t j = NLA_ALIGN(5);
			uint16_t type = nl_get_type(expr->data + j);
			uint16_t len = nl_get_len(expr->data + j);

			for (uint64_t k = j + 4; k < len;) {
				uint16_t nested_len = nl_get_len(expr->data + k);
				uint16_t nested_type = nl_get_type(expr->data + k);
				char *data = nl_get_data(expr->data + k);
				if ((nested_type == NFTA_MASQ_FLAGS) && (nested_len == 8)) {
					masq_flags = to_uint32_swap(data);
				}
				if (nested_type == NFTA_MASQ_REG_PROTO_MIN) {
					masq_to_ports = 1;
				}
				k += NLA_ALIGN(len);
			}
		} else if (!strncmp(expr->data, "redir", 5)) {
			*verdict = ALLIGATOR_NFT_REDIRECT;
		} else if (!strncmp(expr->data, "dup", 3)) {
			*verdict = ALLIGATOR_NFT_DUP;
		} else {
			printf("1unknown type '%s' %ld\n", expr->data, i);
			uint16_t unklen = expr->data[expr->namelen];
			dump_pkt(expr->data, expr->len);
		}

		i += NLA_ALIGN(expr->len);
	}
	if (!updated) {
		nft_add_expr(expression, meta, cmpop, payload_offset, payload_base, lookup, addrsize, portsize, othersize, target, immediate_addr1, immediate_addr2, immediate_port1, immediate_port2, ct_key, states_data, objref, proto, port, reject_icmp_type, reject_icmp_code, goto_chain, ct_state_related, ct_state_new, ct_state_invalid, ct_state_established, ct_state_untracked, log_prefix, log_group, log_snaplen, log_qtreshold, log_level, log_flags, limit_rate, limit_unit, limit_burst, limit_type, limit_flags, numgen_mod, numgen_type, nat_type, nat_family, nat_flags, connlimit, masq_flags, masq_to_ports);
		if (target)
			free(target);
		if (immediate_addr1)
			free(immediate_addr1);
		if (immediate_addr2)
			free(immediate_addr2);
	}
	return expression;
}

void nftables_send_query(int setfd, uint16_t nlmsg_type, int nlmsg_flags, uint32_t pid, uint32_t seq, uint8_t nfgen_family, void *body, size_t body_len, void *userdata) {
	int fd = setfd;
	if (!setfd) {
		fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_NETFILTER);
		if (fd < 0)
			return;
	}
	struct sockaddr_nl nladdr;
	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;
	uint8_t i8type = nlmsg_type;
	uint8_t is_setelem = ((i8type & NFT_MSG_GETSETELEM) == i8type);

	struct {
		struct nlmsghdr nlh; // 16
		struct nfgenmsg nfg; // 4
		char body[NLA_ALIGN(body_len)];
	} req;
	size_t full_len = NLA_ALIGN(sizeof(req));

	memset(&req, 0, full_len);

	req.nlh.nlmsg_len = full_len; // 4
	req.nlh.nlmsg_type = nlmsg_type; // 2
	req.nlh.nlmsg_flags = nlmsg_flags; // 2
	req.nlh.nlmsg_seq = seq; // 4
	req.nlh.nlmsg_pid = pid; // 4

	req.nfg.nfgen_family = nfgen_family;
	//req.nfg.nfgen_family = AF_UNSPEC;
	//req.nfg.nfgen_family = NFPROTO_INET; // 1
	req.nfg.version = NFNETLINK_V0; // 1
	req.nfg.res_id = htons(0); // 2
	if (body_len)
		memcpy(&req.body, body, body_len);

	struct iovec iov[3];
	iov[0] = (struct iovec) {
		.iov_base = &req,
		.iov_len = sizeof(req) + body_len,
	};

	struct msghdr msg;

	msg = (struct msghdr) {
		.msg_name = (void*)&nladdr,
		.msg_namelen = sizeof(nladdr),
		.msg_iov = iov,
		.msg_iovlen = 1,
		.msg_control = 0,
		.msg_controllen = 0,
		.msg_flags = 0
	};

	int rep = sendmsg(fd, &msg, 0);
	if (rep < 0)
	{
		//if (ac->system_carg->log_level > 1)
			printf("sendmsg error: get_conntrack_info\n");
		return;
	}

	char fbuf[getpagesize() * 8];
	while (rep > 0)
	{
		int status;

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

			if (!setfd)
				close(fd);

			if (errno == EINTR)
				return;

			printf("OVERRUN\n");
			return;
		}
		if (status == 0) {
			if (!setfd)
				close(fd);
			return;
		}

		struct nlmsghdr *h = (struct nlmsghdr*)fbuf;

		if (h->nlmsg_seq != seq) {
			if (is_setelem)
				continue;
			else
				break;
		}

		if (h->nlmsg_type == NLMSG_DONE) {
			if (!setfd)
				close(fd);
			return;
		}

		if (h->nlmsg_type == NLMSG_ERROR) {
			nft_genmsg *rawmsg = (nft_genmsg*)NLMSG_DATA(h);
			char *dtchar = rawmsg->data;
			if (!setfd)
				close(fd);
			return;
		}

		while (status > 0 && NLMSG_OK(h, (uint32_t) status))
		{
			nft_str_expression *expression = NULL;
			nft_genmsg *rawmsg = (nft_genmsg*)NLMSG_DATA(h);
			if (h->nlmsg_type == NLMSG_DONE) {
				puts("");
				if (!setfd)
					close(fd);
				return;
			}

			uint64_t dt_size = h->nlmsg_len;
			char *dtchar = rawmsg->data;
			if ((i8type & NFT_MSG_GETRULE) == i8type) {
				char table[1024];
				char chain[1024] = {0};
				char set[1024] = {0};
				char userdata[1024] = { 0 };
				int8_t verdict;
				for (uint64_t i = 0; i + 20 < dt_size;)
				{
					nftable_struct *nftmsg = (nftable_struct*)(dtchar + i);
					uint16_t itype = nftmsg->nlattr.nla_type;
					if (itype == NFTA_RULE_TABLE)
						strcpy(table, nftmsg->data);
					else if (itype == NFTA_RULE_CHAIN)
						strcpy(chain, nftmsg->data);
					else if (itype == NFTA_RULE_EXPRESSIONS)
						expression = parse_expressions(fd, table, chain, nftmsg->data, nftmsg->nlattr.nla_len, &verdict);
					else if (itype == NFTA_RULE_USERDATA)
						strcpy(userdata, nftmsg->data+2);
					else if (itype == NFTA_RULE_HANDLE) {
					}
					else if (itype == NFTA_RULE_POSITION) {
					}
					else {
						printf("2unknown type %d\n", itype);
					}

					if (nftmsg->nlattr.nla_len == 0)
						break;
					if (nftmsg->nlattr.nla_type > 65)
						break;
					i += NLA_ALIGN(nftmsg->nlattr.nla_len);
				}
				char *expstr = nft_expression_deserialize(expression);
				char *verdict_str = nft_get_verdict_by_id(verdict);
				printf("table '%s', chain: '%s', userdata: '%s', expr: '%s', packets %lu, bytes %lu, verdict '%s'\n", table, chain, userdata, expstr, expression->packets, expression->bytes, verdict_str);
				if (expstr)
					free(expstr);
				free(expression);
			}
			else if (((i8type & NFT_MSG_GETSET) == i8type) && !is_setelem) {
				char table[1024];
				char setname[1024] = {0};
				set_t *set = userdata;
				for (uint64_t i = 0; i + 20 < dt_size;)
				{
					nftable_struct *nftmsg = (nftable_struct*)(dtchar + i);
					uint16_t itype = nftmsg->nlattr.nla_type;
					if (itype == NFTA_RULE_TABLE)
						strcpy(table, nftmsg->data);
					else if (itype == NFTA_RULE_CHAIN)
						strcpy(setname, nftmsg->data);
					else if (itype == NFTA_SET_TIMEOUT) {
						set->timeout = to_uint16(nftmsg->data);
					}
					else if (itype == NFTA_SET_FLAGS) {
						uint32_t flags = to_uint32_swap(nftmsg->data);
						set->is_constant = (flags & NFT_SET_CONSTANT) != 0;
						set->is_anonymous = (flags & NFT_SET_ANONYMOUS) != 0;
						set->is_interval = (flags & NFT_SET_INTERVAL) != 0;
						set->is_map = (flags & NFT_SET_MAP) != 0;
						//printf("FIND table %s, setname %s, flags is %b map is %b: %d/%d, result: %d\n", table, setname, flags, NFT_SET_MAP, (flags & NFT_SET_MAP) != 0, (flags & NFT_SET_MAP) == flags, set->is_map);
						set->has_timeout = (flags & NFT_SET_TIMEOUT) != 0;
					}
					else if (itype == NFTA_SET_KEY_TYPE) {
						uint32_t keytype = to_uint32_swap(nftmsg->data);
					}
					else if (itype == NFTA_SET_KEY_LEN) {
						uint32_t keylen = to_uint32_swap(nftmsg->data);
					}
					else if (itype == NFTA_SET_DATA_TYPE) {
						set->datatype = to_uint32_swap(nftmsg->data);
					}
					else if (itype == NFTA_SET_DATA_LEN) {
						uint32_t datalen = to_uint32_swap(nftmsg->data);
					}
					else if (itype == NFTA_SET_ID) {
					}
					else if (itype == NFTA_SET_DESC) {
					}
					else if (itype == NFTA_SET_USERDATA) {
						uint32_t userdata = to_uint32_swap(nftmsg->data);
					}
					else if (itype == NFTA_SET_HANDLE) {
					}
					else if (itype == NFTA_SET_EXPR) {
					}
					else {
						printf("3unknown type %d\n", itype);
					}

					if (nftmsg->nlattr.nla_len == 0)
						break;
					if (nftmsg->nlattr.nla_type > 65)
						break;
					i += NLA_ALIGN(nftmsg->nlattr.nla_len);
				}
				char *expstr = nft_expression_deserialize(expression);

				size_t set_len = strlen(setname) + 1;
				size_t table_len = strlen(table) + 1;
				struct {
					struct nlattr tnlattr;
					char table[NLA_ALIGN(table_len)];
					struct nlattr snlattr;
					char set[NLA_ALIGN(set_len)];
				} body;
				memset(&body, 0, sizeof(body));

				body.tnlattr.nla_len = table_len + sizeof(struct nlattr);
				body.tnlattr.nla_type = 0x1;
				memcpy(&body.table, table, table_len);
				body.snlattr.nla_len = set_len + sizeof(struct nlattr);
				body.snlattr.nla_type = 0x2;

				memcpy(&body.set, setname, set_len);

				nftables_send_query(0, (NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_GETSETELEM, NLM_F_REQUEST|NLM_F_ACK|NLM_F_DUMP, pid, 3, NFPROTO_INET, &body, sizeof(body), set);

				if (set->allsets) {
					if (!set->is_anonymous)
						printf("table '%s', set: '%s', constant %d anonymous %d interval %d map %d timeout %d(%u), size %d/%d/%d\n", table, setname, set->is_constant, set->is_anonymous, set->is_interval, set->is_map, set->has_timeout, set->timeout, set->addr_size, set->port_size, set->other_size);
					memset(set, 0, sizeof(set));
					set->allsets = 1;
				}

				if (expstr)
					free(expstr);
			}
			else if (is_setelem) {
				char table[1024] = { 0 };
				char setname[1024] = { 0 };
				set_t *set = userdata;
				for (uint64_t i = 0; i + 20 < dt_size;)
				{
					nftable_struct *nftmsg = (nftable_struct*)(dtchar + i);
					uint16_t itype = nftmsg->nlattr.nla_type;
					uint16_t ilen = nftmsg->nlattr.nla_len;
					if (itype == NFTA_RULE_TABLE) {
						strcpy(table, nftmsg->data);
					} else if (itype == 2) {
						strcpy(setname, nftmsg->data);
					} else if ((itype == NLMSG_DONE) && (ilen == 4)) {
						break;
					} else if (itype == NFTA_SET_ELEM_KEY) {
						//dump_pkt(dtchar + i, nftmsg->nlattr.nla_len);
					} else if (itype == NFTA_SET_ELEM_KEY_END) {
					} else if (itype == NFTA_SET_ELEM_DATA) {
						//dump_pkt(dtchar + i, nftmsg->nlattr.nla_len);
					} else if (itype == NFTA_SET_ELEM_FLAGS) {
						if (ilen > 8) {
							uint32_t j = 0;
							uint32_t cnt_addr = 0;
							uint32_t cnt_port = 0;
							uint32_t cnt_other = 0;
							for (; j < ilen-4; ) {
								nftable_struct *list_obj = (nftable_struct*)(nftmsg->data+j);
								if (!list_obj->nlattr.nla_len)
									break;

								if (set->is_map) {
									uint32_t id = to_uint32(list_obj->data+8);
									if (id == CT_STATE_RELATED)
										set->ct_state_related = to_uint32_swap(list_obj->data+24);
									else if (id == CT_STATE_NEW)
										set->ct_state_new = to_uint32_swap(list_obj->data+24);
									else if (id == CT_STATE_INVALID)
										set->ct_state_invalid = to_uint32_swap(list_obj->data+24);
									else if (id == CT_STATE_ESTABLISHED)
										set->ct_state_established = to_uint32_swap(list_obj->data+24);
									else if (id == CT_STATE_UNTRACKED)
										set->ct_state_untracked = to_uint32_swap(list_obj->data+24);
									else if ((set->datatype == SET_DATATYPE_IPV4) || (set->datatype == SET_DATATYPE_IPV6))
										++cnt_addr;
									else if (set->datatype == SET_DATATYPE_INET_PROTO)
										++cnt_port;
									else {
										++cnt_other;
									}
								} else if ((list_obj->data[4] == 8) && (list_obj->data[6] == 1) && (list_obj->data[0] == 12)) {
									char *addr = toIpv4(list_obj->data+8, -1);
									++cnt_addr;
									free(addr);
								} else if ((list_obj->data[4] == 6) && (list_obj->data[6] == 1)) {
									uint16_t port = to_uint16_swap(list_obj->data+8);
									++cnt_port;
								}

								j += NLA_ALIGN(list_obj->nlattr.nla_len);
							}
							if (set->is_interval) {
								set->addr_size += (cnt_addr - 1) / 2;
								set->port_size += (cnt_port - 1) / 2;
								set->other_size += (cnt_other - 1) / 2;
							} else {
								set->addr_size += cnt_addr;
								set->port_size += cnt_port;
								set->other_size += cnt_other;
							}
						}
					} else if (itype == NFTA_SET_ELEM_TIMEOUT) {
					} else if (itype == NFTA_SET_ELEM_EXPIRATION) {
					} else if (itype == NFTA_SET_ELEM_EXPR) {
					} else {
						printf("unknown setelem type '%d'\n", itype);
					}
					i += NLA_ALIGN(nftmsg->nlattr.nla_len);
				}

				//printf("table '%s' setname '%s' size %d/%d\n", table, setname, set->addr_size, set->port_size);
			}
			else if (((i8type & NFT_MSG_GETOBJ) == i8type)) {
				char table[1024] = { 0 };
				char counter[1024] = { 0 };
				char userdata[1024] = { 0 };
				uint64_t bytes = 0;
				uint64_t packets = 0;
				for (uint64_t i = 0; i + 20 < dt_size;)
				{
					nftable_struct *nftmsg = (nftable_struct*)(dtchar + i);
					uint16_t itype = nftmsg->nlattr.nla_type;
					uint16_t ilen = nftmsg->nlattr.nla_len;
					if (itype == NFTA_OBJ_TABLE) {
						strcpy(table, nftmsg->data);
					} else if (itype == NFTA_OBJ_NAME) {
						strcpy(counter, nftmsg->data);
					} else if (itype == NFTA_OBJ_DATA) {
						for (uint16_t j = 0; j < nftmsg->nlattr.nla_len; ) {
							uint16_t type = nl_get_type(nftmsg->data + j);
							uint16_t len = nl_get_len(nftmsg->data + j);
							if (!len)
								break;

							char *data = nl_get_data(nftmsg->data + j);
							if (type == NFTA_COUNTER_BYTES) {
								bytes = to_uint64_swap(data);
							} else if (type == NFTA_COUNTER_PACKETS) {
								packets = to_uint64_swap(data);
							} else {
							}

							j += NLA_ALIGN(len);
						}
					} else if (itype == NFTA_OBJ_USERDATA) {
						strcpy(userdata, nftmsg->data + 2);
					}

					i += NLA_ALIGN(nftmsg->nlattr.nla_len);
				}

				printf("obj: table:%s name:%s, bytes %lu, packets %lu userdata: '%s'\n", table, counter, bytes, packets, userdata);
			}
			h = NLMSG_NEXT(h, status);
		}
	}

	if (!setfd)
		close(fd);
}

void nftables_handler()
{
	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_NETFILTER);
	if (fd < 0)
		return;

	pid_t pid = getpid();

	// get rules
	nftables_send_query(fd, (NFNL_SUBSYS_NFTABLES<<8|NFT_MSG_GETRULE), (NLM_F_REQUEST|NLM_F_ACK|NLM_F_ECHO|NLM_F_DUMP), pid, 1, NFPROTO_UNSPEC, NULL, 0, NULL);

	// get non-anon sets
	set_t set = { 0 };
	set.allsets = 1;
	nftables_send_query(fd, (NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_GETSET, NLM_F_REQUEST|NLM_F_ACK|NLM_F_DUMP, pid, 1, NFPROTO_INET, NULL, 0, &set);

	// get custom counters
	nftables_send_query(fd, (NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_GETOBJ, NLM_F_REQUEST|NLM_F_ACK|NLM_F_DUMP, pid, 1, NFPROTO_INET, NULL, 0, NULL);

	close(fd);
}
//int main() {
//	nftables_handler();
//}
#endif
#endif
