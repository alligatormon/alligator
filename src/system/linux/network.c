//#ifdef __linux__
//#include <stdio.h>
//#include <ifaddrs.h>
//#include <netpacket/packet.h>
//#include "main.h"
//#include "common/selector.h"
//
//void get_network_hwaddr()
//{
//	struct ifaddrs *ifaddr=NULL;
//	struct ifaddrs *ifa = NULL;
//	int i = 0;
//	uint64_t okval = 1;
//	string *hwaddr = string_new();
//	char hwpart[4];
//
//	if (getifaddrs(&ifaddr) == -1)
//	{
//		 perror("getifaddrs");
//	}
//	else
//	{
//		for ( ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
//		{
//			if ( (ifa->ifa_addr) && (ifa->ifa_addr->sa_family == AF_PACKET) )
//			{
//				struct sockaddr_ll *s = (struct sockaddr_ll*)ifa->ifa_addr;
//				for (i=0; i <s->sll_halen; i++)
//				{
//					int len = snprintf(hwpart, 3, "%02x%c", (s->sll_addr[i]), (i+1!=s->sll_halen)?':':'\n');
//					string_cat(hwaddr, hwpart, len);
//					printf("concat %s(%s): %s\n", ifa->ifa_name, hwaddr->s, hwpart);
//				}
//				metric_add_labels2("interface_hwaddr", &okval, DATATYPE_UINT, ac->system_carg, "hwaddr", hwaddr->s, "interace", ifa->ifa_name);
//				string_null(hwaddr);
//			}
//		}
//		freeifaddrs(ifaddr);
//	}
//
//	string_free(hwaddr);
//}
//#endif
