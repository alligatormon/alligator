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
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <inttypes.h>
#include <byteswap.h>
#include "dstructures/ht.h"
#include "metric/namespace.h"
#include "main.h"

int sequence_number;

void get_conntrack_info()
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
	} req;

	req.nlh.nlmsg_len = sizeof(req);
	req.nlh.nlmsg_type = htons(1 | (IPCTNL_MSG_CT_GET_STATS << 8));
	req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP;
	req.nlh.nlmsg_pid = 0;
	req.nlh.nlmsg_seq = 2;

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
		close(fd);
		return;
	}

	char fbuf[getpagesize() * 8];
	if (rep > 0)
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
				close(fd);
				return;
			}
			printf("OVERRUN\n");
			{
				close(fd);
				return;
			}
		}
		if (status == 0)
		{
			close(fd);
			return;
		}

		struct nlmsghdr *h = (struct nlmsghdr*)fbuf;

		if (status > 0 && NLMSG_OK(h, (uint32_t) status))
		{
			char *dt = (char*)NLMSG_DATA(h);

			if (h->nlmsg_seq != 2) {
				h = NLMSG_NEXT(h, status);
			}

			if (h->nlmsg_type == NLMSG_DONE || h->nlmsg_type == NLMSG_ERROR)
			{
				status = -1;
				close(fd);
				return;
			}

			uint32_t *kdvalue = (uint32_t*)(dt + 8);
			uint64_t entries = bswap_32(*kdvalue);

			kdvalue = (uint32_t*)(dt + 16);
			uint64_t maxentries = bswap_32(*kdvalue);
			if (!maxentries)
				maxentries = getkvfile("/proc/sys/net/netfilter/nf_conntrack_max");

			double conntrack_usage = entries*100.0/maxentries;

			metric_add_auto("conntrack_max", &maxentries, DATATYPE_UINT, ac->system_carg);
			metric_add_auto("conntrack_count", &entries, DATATYPE_UINT, ac->system_carg);
			metric_add_auto("conntrack_usage", &conntrack_usage, DATATYPE_DOUBLE, ac->system_carg);
		}
	}
	close(fd);
}
#endif
