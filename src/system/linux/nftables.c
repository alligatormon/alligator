#ifdef __linux__
#include <linux/version.h>
# if LINUX_VERSION_CODE >= KERNEL_VERSION (5, 0, 0)
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <netinet/in.h>
#include <errno.h>
#include <byteswap.h>
#include "main.h"

#include <linux/netfilter.h>
#include <linux/netlink.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/netfilter/nfnetlink.h>

extern aconf *ac;

typedef struct nftable_struct
{
	struct nlattr attr;
	char data[];
} nftable_struct;

void nftables_send_query(int fd, uint16_t nlmsg_type, int nlmsg_flags, pid_t pid, int once) {
	struct sockaddr_nl nladdr;
	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;

	struct {
		struct nlmsghdr nlh; // 16
		struct nfgenmsg nfg; // 4
	} req;

	req.nlh.nlmsg_len = sizeof(req);
	//req.nlh.nlmsg_type = (NFNL_SUBSYS_NFTABLES<<8|NFT_MSG_GETGEN);
	req.nlh.nlmsg_type = nlmsg_type;
	//req.nlh.nlmsg_flags = NLM_F_REQUEST;
	req.nlh.nlmsg_flags = nlmsg_flags;
	req.nlh.nlmsg_pid = pid;
	req.nlh.nlmsg_seq = 0;

	req.nfg.nfgen_family = AF_INET;
	req.nfg.version = NFNETLINK_V0;
	req.nfg.res_id = 0;

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
			if (errno == EINTR)
			{
				return;
			}
			printf("OVERRUN\n");
			{
				return;
			}
		}
		if (status == 0)
		{
			return;
		}

		struct nlmsghdr *h = (struct nlmsghdr*)fbuf;
		while (status > 0 && NLMSG_OK(h, (uint32_t) status))
		{
			//char *body = (char*)NLMSG_DATA(h);
			nftable_struct *dt = (nftable_struct*)NLMSG_DATA(h);
			char *dtchar = (char*)dt;

			if (h->nlmsg_type == NLMSG_DONE || h->nlmsg_type == NLMSG_ERROR)
				return;

			if (h->nlmsg_seq != 2) {
				h = NLMSG_NEXT(h, status);
			}

			uint16_t itype = h->nlmsg_type;
			char *type = "";
			if (itype == NFTA_CHAIN_NAME)
				type = "chain";
			else if (itype == NFTA_TABLE_NAME)
				type = "table";
			else if (itype == NFTA_CHAIN_POLICY)
				type = "policy";

			uint64_t dt_size = h->nlmsg_len;
			printf("\ndata: %s\n", dt->data);
			printf("n: %zu, size: %u, type %u\n", sizeof (*dt), dt->attr.nla_len, dt->attr.nla_type);
			printf("len: %u, type %u: %s\n", h->nlmsg_len, h->nlmsg_type, type);
			for (uint64_t i = 4; (i + 4) < dt_size;++i)
			{
				uint16_t size = ((uint8_t)(dtchar[i + 1]) << 8) + (uint8_t)(dtchar[i]);
				uint16_t type = ((uint16_t)*(dtchar + i + 2));
				//if (ac->system_carg->log_level > 0)
					printf("\t1[%"PRIu64"/%"PRIu64"] name: %s, type: %d, size: %d: %hhu %hhu %hhu %hhu %hhu %hhu %hhu %hhu\n", i, dt_size, "", type, size, dtchar[i], dtchar[i+1], dtchar[i+2], dtchar[i+3], dtchar[i+4], dtchar[i+5], dtchar[i+6], dtchar[i+7]);
			}
			//uint32_t *kdvalue = (uint32_t*)(dt + 8);
			//uint64_t entries = bswap_32(*kdvalue);

			//kdvalue = (uint32_t*)(dt + 16);
			//uint64_t maxentries = bswap_32(*kdvalue);
			//if (!maxentries)
			//	maxentries = getkvfile("/proc/sys/net/netfilter/nf_conntrack_max");

			//double conntrack_usage = entries*100.0/maxentries;

			//metric_add_auto("conntrack_max", &maxentries, DATATYPE_UINT, ac->system_carg);
			//metric_add_auto("conntrack_count", &entries, DATATYPE_UINT, ac->system_carg);
			//metric_add_auto("conntrack_usage", &conntrack_usage, DATATYPE_DOUBLE, ac->system_carg);
		}

		if (once)
			break;
	}
}

void nftables_handler()
{
	puts("nftables_handler");
	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_NETFILTER);
	if (fd < 0)
		return;

	pid_t pid = getpid();
	nftables_send_query(fd, NFNL_SUBSYS_NFTABLES<<8|NFT_MSG_GETGEN, NLM_F_REQUEST, pid, 1);
	nftables_send_query(fd, NFNL_SUBSYS_NFTABLES<<8|NFT_MSG_GETTABLE, NLM_F_REQUEST|NLM_F_DUMP, pid, 1);
	nftables_send_query(fd, NFNL_SUBSYS_NFTABLES<<8|NFT_MSG_GETCHAIN, NLM_F_REQUEST|NLM_F_DUMP, pid, 1);
	nftables_send_query(fd, NFNL_SUBSYS_NFTABLES<<8|NFT_MSG_GETSET, NLM_F_REQUEST|NLM_F_DUMP, pid, 1);
	nftables_send_query(fd, NFNL_SUBSYS_NFTABLES<<8|NFT_MSG_GETFLOWTABLE, NLM_F_REQUEST|NLM_F_DUMP, pid, 1);
	nftables_send_query(fd, NFNL_SUBSYS_NFTABLES<<8|NFT_MSG_GETRULE, NLM_F_REQUEST|NLM_F_DUMP, pid, 1);
	nftables_send_query(fd, NFNL_SUBSYS_NFTABLES<<8|NFT_MSG_GETSETELEM, NLM_F_REQUEST|NLM_F_DUMP, pid, 0);
	nftables_send_query(fd, NFNL_SUBSYS_NFTABLES<<8|NFT_MSG_GETOBJ, NLM_F_REQUEST|NLM_F_DUMP, pid, 0);
	nftables_send_query(fd, NFNL_SUBSYS_NFTABLES<<8|NFT_MSG_GETRULE, NLM_F_REQUEST|NLM_F_DUMP, pid, 1);

	close(fd);
}
#else
void nftables_handler()
{
}
#endif
#endif
