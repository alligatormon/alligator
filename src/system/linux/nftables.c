#ifdef __linux__
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0)
#include <linux/version.h>
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
#include <common/selector.h>
#include <common/logs.h>
#include <main.h>

#include <linux/netfilter.h>
#include <linux/netlink.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_log.h>
#include <linux/netfilter/nf_nat.h>

#ifndef NF_NAT_RANGE_NETMAP
#define NF_NAT_RANGE_NETMAP			(1 << 6)
#endif

// for ubuntu 20.04
#ifndef NFTA_SET_EXPR
#define NFTA_SET_EXPR 17
#endif

// for ubuntu 20.04
#ifndef NFTA_SET_ELEM_KEY_END
#define NFTA_SET_ELEM_KEY_END 10
#endif

// for ubuntu 20.04
#ifndef NFTA_OBJ_USERDATA
#define NFTA_OBJ_USERDATA 8
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
extern aconf *ac;


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
	carglog(ac->system_carg, L_OFF, "DEBUG first len %u, status %d\n", h->nlmsg_len, status);
	for (uint64_t i = 0; i < status; ++i) {
		carglog(ac->system_carg, L_OFF, "DEBUG [%ld]: [%hhu]", i, fbuf[i]);
		if (isprint(fbuf[i]))
			carglog(ac->system_carg, L_OFF, "\t'%c'\n", fbuf[i]);
		else
			carglog(ac->system_carg, L_OFF, "\n");
	}
}

typedef struct nft_str_expression {
	//char expression[12][255];
    string_tokens *tokens;
    int debug;
	alligator_ht *label_expression;
	uint64_t packets;
	uint64_t bytes;
	//uint8_t len;
	//uint16_t syms;
    uint64_t addrsize;
    uint64_t portsize;
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

void nft_add_cat(nft_str_expression *expression, char *str) {
	if (expression->debug)
		string_cat(expression->tokens->str[expression->tokens->l-1], str, strlen(str));
}

void nft_add_expr(nft_str_expression *expression, uint8_t meta, int8_t cmpop, int8_t payload_offset, int8_t payload_base, char *lookup, int16_t addrsize, int16_t portsize, int16_t othersize, char *target, char *immediate_addr1, char *immediate_addr2, int32_t immediate_port1, int32_t immediate_port2, int8_t ct_key, int8_t states_data, char *objref, char *proto, uint16_t port, int8_t reject_icmp_type, int8_t reject_icmp_code, char *goto_chain, int8_t ct_state_related, int8_t ct_state_new, int8_t  ct_state_invalid, int8_t ct_state_established, int8_t ct_state_untracked, char *log_prefix, int16_t log_group, int32_t log_snaplen, int16_t log_qtreshold, int32_t log_level, int32_t log_flags, int64_t limit_rate, char* limit_unit, int32_t limit_burst, int32_t limit_type, int32_t limit_flags, int32_t numgen_mod, int32_t numgen_type, int32_t nat_type, int32_t nat_family, int32_t nat_flags, int32_t connlimit, int32_t masq_flags, int32_t masq_to_ports) {
	string_tokens_push_dupn(expression->tokens, "[", 1);
	nft_add_cat(expression, "[");
	if (meta) {
		nft_add_cat(expression, " meta: '");
		if (meta == NFT_META_IIFNAME) {
			nft_add_cat(expression, "iifname");
			labels_hash_insert_nocache(expression->label_expression, "meta", "iifname");
		} else if (meta == NFT_META_OIFNAME) {
			nft_add_cat(expression, "oifname");
			labels_hash_insert_nocache(expression->label_expression, "meta", "oifname");
		} else if (meta == NFT_META_NFPROTO) {
			nft_add_cat(expression, "nfproto");
			labels_hash_insert_nocache(expression->label_expression, "meta", "nfproto");
		} else if (meta == NFT_META_L4PROTO) {
			nft_add_cat(expression, "l4proto");
			labels_hash_insert_nocache(expression->label_expression, "meta", "l4proto");
		}
		nft_add_cat(expression, "'");
	}
	if (cmpop > -1) {
		nft_add_cat(expression, " cmp: '");

		if (cmpop == NFT_CMP_EQ) {
			nft_add_cat(expression, "==");
		} else if (cmpop == NFT_CMP_NEQ) {
			nft_add_cat(expression, "!=");
		} else if (cmpop == NFT_CMP_LT) {
			nft_add_cat(expression, "<");
		} else if (cmpop == NFT_CMP_LTE) {
			nft_add_cat(expression, "<=");
		} else if (cmpop == NFT_CMP_GT) {
			nft_add_cat(expression, ">");
		} else if (cmpop == NFT_CMP_GTE) {
			nft_add_cat(expression, ">=");
		}

		nft_add_cat(expression, "'");
	}
	if (payload_offset > -1) {
		nft_add_cat(expression, " payload offset: '");

		if (payload_offset == PAYLOAD_OFFSET_ADDRLEN) {
			nft_add_cat(expression, "addrlen");
			labels_hash_insert_nocache(expression->label_expression, "payload_offset", "addrlen");
		} else if (payload_offset == PAYLOAD_OFFSET_PORTSRC) {
			nft_add_cat(expression, "sport");
			labels_hash_insert_nocache(expression->label_expression, "payload_offset", "sport");
		} else if (payload_offset == PAYLOAD_OFFSET_PORTDST) {
			nft_add_cat(expression, "dport");
			labels_hash_insert_nocache(expression->label_expression, "payload_offset", "dport");
		} else if (payload_offset == PAYLOAD_OFFSET_ADDRSRC) {
			nft_add_cat(expression, "saddr");
			labels_hash_insert_nocache(expression->label_expression, "payload_offset", "saddr");
		} else if (payload_offset == PAYLOAD_OFFSET_ADDRDST) {
			nft_add_cat(expression, "daddr");
			labels_hash_insert_nocache(expression->label_expression, "payload_offset", "daddr");
		}

		nft_add_cat(expression, "'");
	}
	if (payload_base > -1) {
		nft_add_cat(expression, " payload base: '");
		if (payload_base == NFT_PAYLOAD_NETWORK_HEADER) {
			nft_add_cat(expression, "network");
			labels_hash_insert_nocache(expression->label_expression, "payload_base", "network");
		} else if (payload_base == NFT_PAYLOAD_TRANSPORT_HEADER) {
			nft_add_cat(expression, "transport");
			labels_hash_insert_nocache(expression->label_expression, "payload_base", "transport");
		} else {
			carglog(ac->system_carg, L_ERROR, "unknown payload base: %hhu\n", payload_base);
		}

		nft_add_cat(expression, "'");
	}
	if (*lookup) {
		nft_add_cat(expression, " lookup: '");
		nft_add_cat(expression, lookup);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "lookup", lookup);
	}
	if (addrsize > -1) {
		char setsize_str[10];
		snprintf(setsize_str, 9, "%"PRId16, addrsize);
		nft_add_cat(expression, " addrsize: '");
		nft_add_cat(expression, setsize_str);
		nft_add_cat(expression, "'");
        expression->addrsize += addrsize;
	}
	if (portsize > -1) {
		char setsize_str[10];
		snprintf(setsize_str, 9, "%"PRId16, portsize);
		nft_add_cat(expression, " portize: '");
		nft_add_cat(expression, setsize_str);
		nft_add_cat(expression, "'");
        expression->portsize += portsize;
	}
	if (othersize > -1) {
		char setsize_str[10];
		snprintf(setsize_str, 9, "%"PRId16, othersize);
		nft_add_cat(expression, " otherize: '");
		nft_add_cat(expression, setsize_str);
		nft_add_cat(expression, "'");
	}
	if (target) {
		nft_add_cat(expression, " target: '");
		nft_add_cat(expression, target);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "target", target);
	}
	if ((immediate_addr1) && (!immediate_addr2)) {
		nft_add_cat(expression, " immediate_addr1: '");
		nft_add_cat(expression, immediate_addr1);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "immediate_addr", immediate_addr1);
	}
	else if ((immediate_addr2) && (!immediate_addr1)) {
		nft_add_cat(expression, " immediate_addr2: '");
		nft_add_cat(expression, immediate_addr2);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "immediate_addr", immediate_addr2);
	}
	else if ((immediate_addr1) && (immediate_addr2)) {
		char addrs[128];
		snprintf(addrs, 127, "%s-%s", immediate_addr1, immediate_addr2);
		nft_add_cat(expression, " immediate_addr: '");
		nft_add_cat(expression, addrs);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "immediate_addrs", addrs);
	}
	if ((immediate_port1 > -1) && (immediate_port2 < 0)) {
		char portnum[20];
		snprintf(portnum, 19, "%"PRId32, immediate_port1);
		nft_add_cat(expression, " immediate_port1: '");
		nft_add_cat(expression, portnum);
		nft_add_cat(expression, "'");
		if (masq_to_ports > -1) {
			nft_add_cat(expression, " masquerade [to-ports]");
			labels_hash_insert_nocache(expression->label_expression, "masquerade_to_ports", portnum);
		} else {
			labels_hash_insert_nocache(expression->label_expression, "immediate_ports", portnum);
		}
	}
	else if ((immediate_port2 > -1) && (immediate_port1 < 0)) {
		char portnum[20];
		snprintf(portnum, 19, "%"PRId32, immediate_port2);
		nft_add_cat(expression, " immediate_port2: '");
		nft_add_cat(expression, portnum);
		nft_add_cat(expression, "'");

		if (masq_to_ports > -1) {
			nft_add_cat(expression, " masquerade [to-ports]");
			labels_hash_insert_nocache(expression->label_expression, "masquerade_to_ports", portnum);
		}
	}
	else if ((immediate_port1 > -1) && (immediate_port2 > -1)) {
		char portnum[40];
		snprintf(portnum, 39, "%"PRId32"-%"PRId32, immediate_port1, immediate_port2);
		nft_add_cat(expression, " immediate_ports: '");
		nft_add_cat(expression, portnum);
		nft_add_cat(expression, "'");

		if (masq_to_ports > -1) {
			nft_add_cat(expression, " masquerade [to-ports]");
			labels_hash_insert_nocache(expression->label_expression, "masquerade_to_ports", portnum);
		}
	}
	if (ct_state_related > -1) {
		if (ct_state_related == 1) {
			nft_add_cat(expression, " related: 'accept'");
			labels_hash_insert_nocache(expression->label_expression, "ct_related", "accept");
		} else if (ct_state_related == 0) {
			nft_add_cat(expression, " related: 'drop'");
			labels_hash_insert_nocache(expression->label_expression, "ct_related", "drop");
		}
	}
	if (ct_state_new > -1) {
		if (ct_state_new == 1) {
			nft_add_cat(expression, " new: 'accept'");
			labels_hash_insert_nocache(expression->label_expression, "ct_new", "accept");
		} else if (ct_state_new == 0) {
			nft_add_cat(expression, " new: 'drop'");
			labels_hash_insert_nocache(expression->label_expression, "ct_new", "drop");
		}
	}
	if (ct_state_invalid > -1) {
		if (ct_state_invalid == 1) {
			nft_add_cat(expression, " invalid: 'accept'");
			labels_hash_insert_nocache(expression->label_expression, "ct_invalid", "accept");
		} else if (ct_state_invalid == 0) {
			nft_add_cat(expression, " invalid: 'drop'");
			labels_hash_insert_nocache(expression->label_expression, "ct_invalid", "drop");
		}
	}
	if (ct_state_established > -1) {
		if (ct_state_established == 1) {
			nft_add_cat(expression, " established: 'accept'");
			labels_hash_insert_nocache(expression->label_expression, "ct_established", "accept");
		} else if (ct_state_established == 0) {
			nft_add_cat(expression, " established: 'drop'");
			labels_hash_insert_nocache(expression->label_expression, "ct_established", "drop");
		}
	}
	if (ct_state_untracked > -1) {
		if (ct_state_untracked == 1) {
			nft_add_cat(expression, " untracked: 'accept'");
			labels_hash_insert_nocache(expression->label_expression, "ct_untracked", "accept");
		} else if (ct_state_untracked == 0) {
			nft_add_cat(expression, " untracked: 'drop'");
			labels_hash_insert_nocache(expression->label_expression, "ct_untracked", "drop");
		}
	}
	if (ct_key > -1) {
		nft_add_cat(expression, " ct key: '");
		if (ct_key == NFT_CT_STATE) {
			nft_add_cat(expression, "state");
		}
		nft_add_cat(expression, "'");
	}
	if (states_data > -1) {
		nft_add_cat(expression, " states: [");
		string *states = string_init(50);
		uint8_t opened = 0;
		if ((states_data & CT_STATE_INVALID) > 0) {
			string_cat(states, "INVALID", 7);
			opened = 1;
		}
		if ((states_data & CT_STATE_ESTABLISHED) > 0) {
			if (opened)
				string_cat(states, ", ", 2);
			string_cat(states, "ESTABLISHED", 11);
			opened = 1;
		}
		if ((states_data & CT_STATE_RELATED) > 0) {
			if (opened)
				string_cat(states, ", ", 2);
			string_cat(states, "RELATED", 7);
			opened = 1;
		}
		if ((states_data & CT_STATE_NEW) > 0) {
			if (opened)
				string_cat(states, ", ", 2);
			string_cat(states, "NEW", 3);
			opened = 1;
		}
		if ((states_data & CT_STATE_UNTRACKED) > 0) {
			if (opened)
				string_cat(states, ", ", 2);
			string_cat(states, "UNTRACKED", 9);
			opened = 1;
		}
		nft_add_cat(expression, states->s);
		nft_add_cat(expression, "]");

		labels_hash_insert_nocache(expression->label_expression, "ct_states", states->s);
        string_free(states);
	}
	if (*objref) {
		nft_add_cat(expression, " objref: '");
		nft_add_cat(expression, objref);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "objref", objref);
	}
	if (proto) {
		nft_add_cat(expression, " proto: '");
		nft_add_cat(expression, proto);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "proto", proto);
	}
	if (port) {
		char portnum[10];
		snprintf(portnum, 9, "%"PRIu16, port);
		nft_add_cat(expression, " port: '");
		nft_add_cat(expression, portnum);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "port", portnum);
	}
	if (reject_icmp_code > -1) {
		char type_str[8];
        string *reject_icmp = string_init(64);
		snprintf(type_str, 7, "%"PRId16, reject_icmp_type);

		nft_add_cat(expression, " reject icmp_type: '");
		nft_add_cat(expression, type_str);
		labels_hash_insert_nocache(expression->label_expression, "reject_icmp_type", type_str);

		nft_add_cat(expression, "', icmp_code: '");
		if (reject_icmp_code == NFT_REJECT_ICMPX_NO_ROUTE) {
			nft_add_cat(expression, "no route");
            string_cat(reject_icmp, "no route", 8);
		} else if (reject_icmp_code == NFT_REJECT_ICMPX_PORT_UNREACH) {
			nft_add_cat(expression, "port unreachable");
            string_cat(reject_icmp, "port unreachable", 16);
		} else if (reject_icmp_code == NFT_REJECT_ICMPX_HOST_UNREACH) {
			nft_add_cat(expression, "host unreachable");
            string_cat(reject_icmp, "host unreachable", 16);
		} else if (reject_icmp_code == NFT_REJECT_ICMPX_ADMIN_PROHIBITED) {
			nft_add_cat(expression, "admin prohibited");
            string_cat(reject_icmp, "admin prohibited", 16);
		}
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "reject_icmp_code", reject_icmp->s);
        string_free(reject_icmp);
	}
	if (*goto_chain) {
		nft_add_cat(expression, " goto: '");
		nft_add_cat(expression, goto_chain);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "goto", goto_chain);
	}
	if (*log_prefix) {
		nft_add_cat(expression, " log prefix: '");
		nft_add_cat(expression, log_prefix);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "log_prefix", log_prefix);
	}
	if (log_group > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId16, log_group);
		nft_add_cat(expression, " log group: '");
		nft_add_cat(expression, str);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "log_group", str);
	}
	if (log_snaplen > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId32, log_snaplen);
		nft_add_cat(expression, " log snaplen: '");
		nft_add_cat(expression, str);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "log_snaplen", str);
	}
	if (log_qtreshold > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId32, log_qtreshold);
		nft_add_cat(expression, " log qtreshold: '");
		nft_add_cat(expression, str);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "log_qtreshold", str);
	}
	if (log_level > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId32, log_level);
		nft_add_cat(expression, " log_level: '");
		nft_add_cat(expression, str);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "log_level", str);
	}
	if (log_flags > -1) {
		string *logflags = string_init(128);
		nft_add_cat(expression, " log_flags: [");
		uint8_t opened = 0;
		if ((log_flags & NF_LOG_TCPSEQ) > 0) {
			string_cat(logflags, "'tcp seq'", 9);
			opened = 1;
		}
		if ((log_flags & NF_LOG_TCPOPT) > 0) {
			if (opened)
				string_cat(logflags, ", ", 2);
			string_cat(logflags, "'tcp opt'", 9);
			opened = 1;
		}
		if ((log_flags & NF_LOG_IPOPT) > 0) {
			if (opened)
				string_cat(logflags, ", ", 2);
			string_cat(logflags, "'ip opt'", 8);
			opened = 1;
		}
		if ((log_flags & NF_LOG_UID) > 0) {
			if (opened)
				string_cat(logflags, ", ", 2);
			string_cat(logflags, "'uid'", 5);
			opened = 1;
		}
		if ((log_flags & NF_LOG_NFLOG) > 0) {
			if (opened)
				string_cat(logflags, ", ", 2);
			string_cat(logflags, "'nflog'", 7);
			opened = 1;
		}
		if ((log_flags & NF_LOG_MACDECODE) > 0) {
			if (opened)
				string_cat(logflags, ", ", 2);
			string_cat(logflags, "'macdecode'", 11);
			opened = 1;
		}
		if ((log_flags & NF_LOG_MASK) > 0) {
			if (opened)
				string_cat(logflags, ", ", 2);
			string_cat(logflags, "'mask'", 6);
			opened = 1;
		}
		nft_add_cat(expression, logflags->s);
		nft_add_cat(expression, "]");
		labels_hash_insert_nocache(expression->label_expression, "log_level", logflags->s);
        string_free(logflags);
	}
	if (limit_rate > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId64, limit_rate);
		nft_add_cat(expression, " limit_rate: '");
		nft_add_cat(expression, str);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "limit_rate", str);
	}
	if (*limit_unit) {
		nft_add_cat(expression, " limit_unit: '");
		nft_add_cat(expression, limit_unit);
		nft_add_cat(expression, "'");
	}
	if (limit_burst > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId32, limit_burst);
		nft_add_cat(expression, " limit_burst: '");
		nft_add_cat(expression, str);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "limit_burst", str);
	}
	if (limit_type > -1) {
		if (limit_type == NFT_LIMIT_PKTS) {
			nft_add_cat(expression, " limit_type: 'packets'");
			labels_hash_insert_nocache(expression->label_expression, "limit_type", "packets");
		} else if (limit_type == NFT_LIMIT_PKT_BYTES) {
			nft_add_cat(expression, " limit_type: 'bytes'");
			labels_hash_insert_nocache(expression->label_expression, "limit_burst", "bytes");
		}
	}
	if (limit_flags > -1) {
		nft_add_cat(expression, " limit_flags: [");
		uint8_t opened = 0;
		if ((limit_flags & NFT_LIMIT_F_INV) > 0) {
			if (opened)
				nft_add_cat(expression, ", ");
			nft_add_cat(expression, "'over'");
			labels_hash_insert_nocache(expression->label_expression, "limit_flags", "over");
			opened = 1;
		}
	}
	if (numgen_mod > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId32, numgen_mod);
		nft_add_cat(expression, " numgen mod: '");
		nft_add_cat(expression, str);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "numgen_mod", str);
	}

	if (numgen_type > -1) {
		nft_add_cat(expression, " numgen type: ");
		if ((numgen_type & NFT_NG_INCREMENTAL) > 0) {
			nft_add_cat(expression, "'round-robin'");
			labels_hash_insert_nocache(expression->label_expression, "numgen_type", "round-robin");
		} else if ((numgen_type & NFT_NG_RANDOM) > 0) {
			nft_add_cat(expression, "'random'");
			labels_hash_insert_nocache(expression->label_expression, "numgen_type", "random");
		}
	}

	if (nat_type > -1) {
		nft_add_cat(expression, " nat type: ");
		if (nat_type == NFT_NAT_SNAT) {
			nft_add_cat(expression, "'snat'");
			labels_hash_insert_nocache(expression->label_expression, "nat_type", "snat");
		} else if (nat_type == NFT_NAT_DNAT) {
			nft_add_cat(expression, "'dnat'");
			labels_hash_insert_nocache(expression->label_expression, "nat_type", "dnat");
		}
	}
	if (nat_flags > -1) {
		string *natflags = string_init(128);
		nft_add_cat(expression, " nat_flags: [");
		uint8_t opened = 0;
		if ((nat_flags & NF_NAT_RANGE_PERSISTENT) != 0) {
			string_cat(natflags, "'persistent'", 12);
			opened = 1;
		}
		if ((nat_flags & NF_NAT_RANGE_PROTO_RANDOM) != 0) {
			if (opened)
				string_cat(natflags, ", ", 2);
			string_cat(natflags, "'random'", 8);
			opened = 1;
		}
		if ((nat_flags & NF_NAT_RANGE_PROTO_RANDOM_FULLY) != 0) {
			if (opened)
				string_cat(natflags, ", ", 2);
			string_cat(natflags, "'random-fully'", 14);
			opened = 1;
		}
		if ((nat_flags & NF_NAT_RANGE_NETMAP) != 0) {
			if (opened)
				string_cat(natflags, ", ", 2);
			string_cat(natflags, "'prefix'", 8);
			opened = 1;
		}
		if ((nat_flags & NF_NAT_RANGE_PROTO_SPECIFIED) != 0) {
			if (opened)
				string_cat(natflags, ", ", 2);
			string_cat(natflags, "'range-proto-specified'", 23);
			opened = 1;
		}
		nft_add_cat(expression, natflags->s);
		nft_add_cat(expression, "]");
		labels_hash_insert_nocache(expression->label_expression, "nat_flags", natflags->s);
		string_free(natflags);
	}
	if (connlimit > -1) {
		char str[16];
		snprintf(str, 15, "%"PRId32, connlimit);
		nft_add_cat(expression, " connlimit: '");
		nft_add_cat(expression, str);
		nft_add_cat(expression, "'");
		labels_hash_insert_nocache(expression->label_expression, "connlimit", str);
	}
	if (masq_flags > -1) {
		string *masqflags = string_init(128);
		nft_add_cat(expression, " masquerade_flags: [");
		uint8_t opened = 0;
		if ((masq_flags & NF_NAT_RANGE_PERSISTENT) != 0) {
			string_cat(masqflags, "'persistent'", 12);
			opened = 1;
		}
		if ((masq_flags & NF_NAT_RANGE_PROTO_RANDOM) != 0) {
			if (opened)
				string_cat(masqflags, ", ", 2);
			string_cat(masqflags, "'random'", 8);
			opened = 1;
		}
		if ((masq_flags & NF_NAT_RANGE_PROTO_RANDOM_FULLY) != 0) {
			if (opened)
				string_cat(masqflags, ", ", 2);
			string_cat(masqflags, "'random-fully'", 14);
			opened = 1;
		}
		nft_add_cat(expression, masqflags->s);
		nft_add_cat(expression, "]");
		labels_hash_insert_nocache(expression->label_expression, "masquerade_opt", "to-ports");
		labels_hash_insert_nocache(expression->label_expression, "masquerade_flags", masqflags->s);
		string_free(masqflags);
	}


	nft_add_cat(expression, "]");
	//char expr_num[63];
	//snprintf(expr_num, 63, "expression%d", expression->len);;
	//labels_hash_insert_nocache(expression->label_expression, expr_num, expression->expression[expression->len]);
	//expression->syms += strlen(expression->expression[expression->len]);
	//expression->len += 1;
	//string_tokens_push_dupn(expression->tokens, "", 0);
}

string* nft_expression_deserialize(nft_str_expression* expression) {
	if (!expression)
		return NULL;

	return string_tokens_join(expression->tokens, ", ", 2);
	//char *exp = calloc(1, expression->syms + 1);

	//for (uint8_t i = 0; i < expression->len; ++i) {
	//	strcat(exp, expression->expression[i]);
	//}

	//return exp;
}

nft_str_expression* parse_expressions(int fd, char *table, char *chain, char *data, uint64_t size, int8_t *verdict, alligator_ht *label_expression) {
	char *curdt;
	uint64_t i = 0;
	int8_t cmpop = -1;
	int meta = 0;
	int updated = 0;
	int prevcmp = 0;
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
	expression->debug = ac->system_carg->log_level >= L_INFO;
	expression->tokens = string_tokens_new();
	expression->label_expression = label_expression;
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
				uint16_t plen = nl_get_len(expr->data + j);
				if (!plen)
					break;
				uint16_t ptype = nl_get_type(expr->data + j);
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
			//dump_pkt(expr->data, expr->len);
			for (uint64_t j = 0; j < expr->len; ) {
				uint64_t jnamelen = expr->namelen+j;
				uint16_t cmplen = nl_get_len(expr->data + jnamelen);
				uint16_t cmptype = nl_get_type(expr->data + jnamelen);
				if (!cmplen)
					break;

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
							carglog(ac->system_carg, L_ERROR, "unknown proto: %d\n", proto_id);
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

			// to explain what you need to assemble:
			//struct {
			//	struct nlattr tnlattr;
			//	char table[NLA_ALIGN(table_len)];
			//	struct nlattr snlattr;
			//	char set[NLA_ALIGN(lookup_len)];
			//} body;

			size_t buffer_size = sizeof(struct nlattr) + NLA_ALIGN(table_len) + sizeof(struct nlattr) + NLA_ALIGN(lookup_len);
			char buffer[buffer_size];
			memset(buffer, 0, buffer_size);
			struct nlattr *tnlattr = (struct nlattr*)buffer;

			tnlattr->nla_len = table_len + sizeof(struct nlattr);
			tnlattr->nla_type = 0x1;
			memcpy(buffer + sizeof(struct nlattr), table, table_len);

			char* ptr_to_set = buffer + sizeof(struct nlattr) + NLA_ALIGN(table_len);
			struct nlattr *snlattr = (struct nlattr*)ptr_to_set;

			snlattr->nla_len = lookup_len + sizeof(struct nlattr);
			snlattr->nla_type = 0x2;

			memcpy(ptr_to_set + sizeof(struct nlattr), lookup, lookup_len);


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
				uint16_t ctlen = nl_get_len(expr->data + j);
				if (!ctlen)
					break;
				uint16_t cttype = nl_get_type(expr->data + j);
				if (cttype == NFTA_CT_KEY) {
					ct_key = expr->data[j+7];
				} else if (cttype == NFTA_CT_DIRECTION) {
					//ct_direction = expr->data[j+7];
				}

				j += NLA_ALIGN(ctlen);
			}
		} else if (!strncmp(expr->data, "objref", 6)) {
			strlcpy(objref, expr->data + expr->namelen+5, 255);
		} else if (!strncmp(expr->data, "counter", 7)) {
			uint16_t counter_size = expr->data[expr->namelen - 4];
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
					updated = 0;
				}
				//new
				else {
					for (uint32_t j = 16; j < expr->len - 15; ) {
						uint16_t type = nl_get_type(expr->data + j);
						uint16_t len = nl_get_len(expr->data + j);
						if (!len)
							break;
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
			uint16_t im_size = to_uint16(expr->data + expr->namelen -3) + expr->namelen;
			for (uint16_t j = expr->namelen+1; j < im_size; ) {
				uint16_t imlen = nl_get_len(expr->data + j);
				if (!imlen)
					break;
				uint16_t rejecttype = nl_get_type(expr->data + j);
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
				uint16_t bitwise_len = nl_get_len(expr->data + j);
				if (!bitwise_len)
					break;
				uint16_t bittype = nl_get_type(expr->data + j);
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
			uint16_t len = nl_get_len(expr->data + j);

			for (uint64_t k = j + 4; k < expr->len;) {
				uint16_t nested_len = nl_get_len(expr->data + k);
				if (!nested_len)
					break;
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
			uint16_t len = nl_get_len(expr->data + j);

			for (uint64_t k = j + 4; k < len;) {
				uint16_t nested_len = nl_get_len(expr->data + k);
				if (!nested_len)
					break;
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
			carglog(ac->system_carg, L_ERROR, "1unknown type '%s' %ld\n", expr->data, i);
			//dump_pkt(expr->data, expr->len);
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
		char body[72];
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
		carglog(ac->system_carg, L_ERROR, "sendmsg error: get_conntrack_info\n");
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

			carglog(ac->system_carg, L_ERROR, "OVERRUN\n");
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
			if (!setfd)
				close(fd);
			return;
		}

		while (status > 0 && NLMSG_OK(h, (uint32_t) status))
		{
			nft_str_expression *expression = NULL;
			nft_genmsg *rawmsg = (nft_genmsg*)NLMSG_DATA(h);
			if (h->nlmsg_type == NLMSG_DONE) {
				carglog(ac->system_carg, L_INFO, "");
				if (!setfd)
					close(fd);
				return;
			}

			uint64_t dt_size = h->nlmsg_len;
			char *dtchar = rawmsg->data;
			if ((i8type & NFT_MSG_GETRULE) == i8type) {

				alligator_ht *label_expression = alligator_ht_init(NULL);
				char table[1024];
				char chain[1024] = {0};
				char userdata[1024] = { 0 };
				int8_t verdict;
				for (uint64_t i = 0; i + 20 < dt_size;)
				{
					nftable_struct *nftmsg = (nftable_struct*)(dtchar + i);
					if (!nftmsg->nlattr.nla_len)
						break;
					labels_hash_insert_nocache(label_expression, "handler", "nftables");
					uint16_t itype = nftmsg->nlattr.nla_type;
					if (itype == NFTA_RULE_TABLE) {
						strcpy(table, nftmsg->data);
						labels_hash_insert_nocache(label_expression, "table", table);
					}
					else if (itype == NFTA_RULE_CHAIN) {
						strcpy(chain, nftmsg->data);
						labels_hash_insert_nocache(label_expression, "chain", chain);
					}
					else if (itype == NFTA_RULE_EXPRESSIONS)
						expression = parse_expressions(fd, table, chain, nftmsg->data, nftmsg->nlattr.nla_len, &verdict, label_expression);
					else if (itype == NFTA_RULE_USERDATA) {
						strcpy(userdata, nftmsg->data+2);
						labels_hash_insert_nocache(label_expression, "chain", chain);
					}
					else if (itype == NFTA_RULE_HANDLE) {
					}
					else if (itype == NFTA_RULE_POSITION) {
					}
					else {
						carglog(ac->system_carg, L_ERROR, "2unknown type %d\n", itype);
					}

					if (nftmsg->nlattr.nla_len == 0)
						break;
					if (nftmsg->nlattr.nla_type > 65)
						break;
					i += NLA_ALIGN(nftmsg->nlattr.nla_len);
				}

				string *expstr = nft_expression_deserialize(expression);
				char *verdict_str = nft_get_verdict_by_id(verdict);
				labels_hash_insert_nocache(label_expression, "userdata", userdata);
				labels_hash_insert_nocache(label_expression, "verdict", verdict_str);

				alligator_ht *bytes_expressions = labels_dup(label_expression);
				alligator_ht *addrsize_expressions = labels_dup(label_expression);
				alligator_ht *port_expressions = labels_dup(label_expression);
				metric_add("firewall_packets_total", label_expression, &expression->packets, DATATYPE_UINT, ac->cadvisor_carg);
				metric_add("firewall_bytes_total", bytes_expressions, &expression->bytes, DATATYPE_UINT, ac->cadvisor_carg);
				metric_add("nfset_address_total", addrsize_expressions, &expression->addrsize, DATATYPE_UINT, ac->cadvisor_carg);
				metric_add("nfset_ports_total", port_expressions, &expression->portsize, DATATYPE_UINT, ac->cadvisor_carg);

				carglog(ac->system_carg, L_INFO, "table '%s', chain: '%s', userdata: '%s', expr: '%s', packets %lu, bytes %lu, verdict '%s'\n", table, chain, userdata, expstr->s, expression->packets, expression->bytes, verdict_str);

				if (expstr)
					string_free(expstr);
                string_tokens_free(expression->tokens);
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
					if (!nftmsg->nlattr.nla_len)
						break;
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
						//uint32_t keytype = to_uint32_swap(nftmsg->data);
					}
					else if (itype == NFTA_SET_KEY_LEN) {
						//uint32_t keylen = to_uint32_swap(nftmsg->data);
					}
					else if (itype == NFTA_SET_DATA_TYPE) {
						set->datatype = to_uint32_swap(nftmsg->data);
					}
					else if (itype == NFTA_SET_DATA_LEN) {
						//uint32_t datalen = to_uint32_swap(nftmsg->data);
					}
					else if (itype == NFTA_SET_ID) {
					}
					else if (itype == NFTA_SET_DESC) {
					}
					else if (itype == NFTA_SET_USERDATA) {
						//uint32_t userdata = to_uint32_swap(nftmsg->data);
					}
					else if (itype == NFTA_SET_HANDLE) {
					}
					else if (itype == NFTA_SET_EXPR) {
					}
					else {
						carglog(ac->system_carg, L_ERROR, "3unknown type %d\n", itype);
					}

					if (nftmsg->nlattr.nla_len == 0)
						break;
					if (nftmsg->nlattr.nla_type > 65)
						break;
					i += NLA_ALIGN(nftmsg->nlattr.nla_len);
				}
				string *expstr = nft_expression_deserialize(expression);

				size_t set_len = strlen(setname) + 1;
				size_t table_len = strlen(table) + 1;

				// to explain what you need to assemble:
				//struct {
				//	struct nlattr tnlattr;
				//	char table[NLA_ALIGN(table_len)];
				//	struct nlattr snlattr;
				//	char set[NLA_ALIGN(set_len)];
				//} body;

				size_t buffer_size = sizeof(struct nlattr) + NLA_ALIGN(table_len) + sizeof(struct nlattr) + NLA_ALIGN(set_len);
				char buffer[buffer_size];
				memset(buffer, 0, buffer_size);
				struct nlattr *tnlattr = (struct nlattr*)buffer;

				tnlattr->nla_len = table_len + sizeof(struct nlattr);
				tnlattr->nla_type = 0x1;
				memcpy(buffer + sizeof(struct nlattr), table, table_len);

				char* ptr_to_set = buffer + sizeof(struct nlattr) + NLA_ALIGN(table_len);
				struct nlattr *snlattr = (struct nlattr*)ptr_to_set;

				snlattr->nla_len = set_len + sizeof(struct nlattr);
				snlattr->nla_type = 0x2;

				memcpy(ptr_to_set + sizeof(struct nlattr), setname, set_len);

				nftables_send_query(0, (NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_GETSETELEM, NLM_F_REQUEST|NLM_F_ACK|NLM_F_DUMP, pid, 3, NFPROTO_INET, buffer, buffer_size, set);

				if (set->allsets) {
					if (!set->is_anonymous)
						carglog(ac->system_carg, L_INFO, "table '%s', set: '%s', constant %d anonymous %d interval %d map %d timeout %d(%u), size %d/%d/%d\n", table, setname, set->is_constant, set->is_anonymous, set->is_interval, set->is_map, set->has_timeout, set->timeout, set->addr_size, set->port_size, set->other_size);
					memset(set, 0, sizeof(struct set_t));
					set->allsets = 1;
				}

				if (expstr)
					string_free(expstr);
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
					if (!ilen)
						break;
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
									//uint16_t port = to_uint16_swap(list_obj->data+8);
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
						carglog(ac->system_carg, L_ERROR, "unknown setelem type '%d'\n", itype);
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
					if (!nftmsg->nlattr.nla_len)
						break;
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

				carglog(ac->system_carg, L_INFO, "obj: table:%s name:%s, bytes %lu, packets %lu userdata: '%s'\n", table, counter, bytes, packets, userdata);
			}
			h = NLMSG_NEXT(h, status);
		}
	}

	if (!setfd)
		close(fd);
}

int nftables_handler()
{
	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_NETFILTER);
	if (fd < 0)
		return 0;

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
    return 1;
}
//int main() {
//	nftables_handler();
//}
#else
int nftables_handler() { return 1; }
#endif
#endif
