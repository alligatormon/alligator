#ifdef __linux__
#include <stdio.h> 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <arpa/inet.h> 
#include <netdb.h> 
#include <unistd.h> 
#include <string.h> 
#include <stdlib.h> 
#include <netinet/ip_icmp.h> 
#include <time.h> 
#include <fcntl.h> 
#include <signal.h> 
#include <time.h> 
#include <uv.h> 
#include "main.h"
  
// Define the Packet Constants 
// ping packet size 
#define PING_PKT_S 64 
   
// Gives the timeout delay for receiving packets 
// in seconds 
#define RECV_TIMEOUT 1  

typedef struct icmp_info
{
	struct sockaddr_in *dest;
	char *key;
	char *address;

	tommy_node node;
} icmp_info;
 
struct ping_pkt 
{ 
	struct icmphdr hdr; 
	char msg[PING_PKT_S-sizeof(struct icmphdr)]; 
}; 
  
unsigned short checksum(void *b, int len) 
{	unsigned short *buf = b; 
	unsigned int sum=0; 
	unsigned short result; 
  
	for ( sum = 0; len > 1; len -= 2 ) 
		sum += *buf++; 
	if ( len == 1 ) 
		sum += *(unsigned char*)buf; 
	sum = (sum >> 16) + (sum & 0xFFFF); 
	sum += (sum >> 16); 
	result = ~sum; 
	return result; 
} 
  
void send_ping(int ping_sockfd, struct sockaddr_in *ping_addr, icmp_info *iinfo)
{ 
	int ttl_val=64, msg_count=0, i, flag=1, msg_received_count=0; 
	socklen_t addr_len;
	  
	struct ping_pkt pckt; 
	struct sockaddr_in r_addr; 
	struct timespec time_start, time_end, tfs, tfe; 
	//double rtt_msec=0;
	double total_msec=0;
	struct timeval tv_out; 
	tv_out.tv_sec = RECV_TIMEOUT; 
	tv_out.tv_usec = 0; 
  
	clock_gettime(CLOCK_MONOTONIC, &tfs); 
  
	if (setsockopt(ping_sockfd, SOL_IP, IP_TTL,  &ttl_val, sizeof(ttl_val)) != 0) 
	{ 
		//printf("\nSetting socket options  to TTL failed!\n"); 
		return; 
	} 
  
	else
	{ 
		//printf("\nSocket set to TTL..\n"); 
	} 
  
	setsockopt(ping_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_out, sizeof tv_out); 
	int pingloop = 1;
	//char *ip = inet_ntoa(iinfo->dest->sin_addr);
  
	while(pingloop--) 
	{ 
		// flag is whether packet was sent or not 
		flag=1; 
	   
		//filling packet 
		bzero(&pckt, sizeof(pckt)); 
		  
		pckt.hdr.type = ICMP_ECHO; 
		pckt.hdr.un.echo.id = getpid(); 
		  
		for ( i = 0; i < sizeof(pckt.msg)-1; i++ ) 
			pckt.msg[i] = i+'0'; 
		  
		pckt.msg[i] = 0; 
		pckt.hdr.un.echo.sequence = msg_count++; 
		pckt.hdr.checksum = checksum(&pckt, sizeof(pckt)); 
  
		clock_gettime(CLOCK_MONOTONIC, &time_start); 
		if ( sendto(ping_sockfd, &pckt, sizeof(pckt), 0,  (struct sockaddr*) ping_addr,  sizeof(*ping_addr)) <= 0) 
		{ 
			//printf("\nPacket Sending Failed!\n"); 
			flag=0; 
		} 
  
		addr_len=sizeof(r_addr); 
  
		if ( recvfrom(ping_sockfd, &pckt, sizeof(pckt), 0,  (struct sockaddr*)&r_addr, &addr_len) <= 0 && msg_count>1)  
		{ 
			//printf("\nPacket receive failed!\n"); 
		} 
  
		else
		{ 
			clock_gettime(CLOCK_MONOTONIC, &time_end); 
			  
			//printf ("timeElapsed=(%"d64"-%"d64")/1000000.0\n", time_end.tv_nsec, time_start.tv_nsec);
			//double timeElapsed = ((double)(time_end.tv_nsec -  time_start.tv_nsec))/1000000.0;
			//printf ("rtt=(%"d64"-%"d64")*1000.0 + (te)%lf\n", time_end.tv_sec, time_start.tv_sec, timeElapsed);
			//rtt_msec = (time_end.tv_sec- time_start.tv_sec) * 1000.0 + timeElapsed; 
			//printf ("rtt=%lf\n", rtt_msec);
			  
			// if packet was not sent, don't receive 
			if(flag) 
			{ 
				if(!(pckt.hdr.type ==69 && pckt.hdr.code==0))  
				{ 
					//printf("Error..Packet received with ICMP  type %d code %d\n",  pckt.hdr.type, pckt.hdr.code); 
				} 
				else
				{ 
					//printf("%d bytes from %s (%s) msg_seq=%d ttl=%d  rtt = %lf ms.\n",  PING_PKT_S, iinfo->key, ip, msg_count, ttl_val, rtt_msec); 
					msg_received_count++; 
				} 
			} 
		}	 
	} 
	clock_gettime(CLOCK_MONOTONIC, &tfe); 


	double timeElapsed = ((double)(tfe.tv_nsec -  tfs.tv_nsec))/1000000.0; 
	  
	total_msec = (tfe.tv_sec-tfs.tv_sec)*1000.0+  timeElapsed;
					 
	//printf("\n===%s ping statistics===\n", ip);
	int64_t loss = msg_count - msg_received_count;
	//printf("\n%d packets sent, %d packets received, %f percent packet loss. Total time: %lf ms.\n\n",  msg_count, msg_received_count, (loss/msg_count) * 100.0, total_msec);  
	//fprintf(stderr, "DONE %s->%s\n", iinfo->address, iinfo->key);
	metric_labels_add_lbl2("aggregator_connect_time", &total_msec, ALLIGATOR_DATATYPE_DOUBLE, 0, "type", "icmp", "host", iinfo->address);
	metric_labels_add_lbl2("aggregator_packet_loss", &loss, ALLIGATOR_DATATYPE_INT, 0, "type", "icmp", "host", iinfo->address);
	close(ping_sockfd);
} 
  
// Driver Code 
void do_icmp(void *arg)
{ 
	icmp_info *iinfo = arg;
	int sockfd; 
	struct sockaddr_in addr_con; 
  
	//ip_addr = dns_lookup(icmp, &addr_con); 
	//if(ip_addr==NULL) 
	//{ 
	//	printf("\nDNS lookup failed! Could  not resolve hostname!\n"); 
	//	return 0; 
	//} 
  
	sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP); 
	if(sockfd<0) 
	{ 
		//printf("\nSocket file descriptor not received!!\n"); 
		return; 
	} 
  
	send_ping(sockfd, &addr_con, iinfo); 
} 

void on_run_icmp(void* arg)
{
	icmp_info *iinfo = arg;

	//uv_thread_t th;
	//uv_thread_create(&th, do_icmp, iinfo);
	do_icmp(iinfo);
}


//static void icmp_timer_cb(uv_timer_t* handle) {
static void icmp_timer_cb(void *arg) {
	extern aconf* ac;
	usleep(ac->iggregator_startup * 1000);
	while ( 1 )
	{
		tommy_hashdyn_foreach(ac->iggregator, on_run_icmp);
		usleep(ac->iggregator_repeat * 1000);
	}
}

void icmp_client_handler()
{
	extern aconf* ac;

	//uv_timer_t *timer1 = calloc(1, sizeof(*timer1));
	//uv_timer_init(loop, timer1);
	//uv_timer_start(timer1, icmp_timer_cb, ac->iggregator_startup, ac->iggregator_repeat);
	uv_thread_t th;
	uv_thread_create(&th, icmp_timer_cb, NULL);
}

void on_resolved(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res)
{
	extern aconf* ac;
	icmp_info *iinfo = resolver->data;

	if (status == -1 || !res) {
		fprintf(stderr, "getaddrinfo callback error\n");
		return;
	}

	char *addr = calloc(17, sizeof(*addr));
	uv_ip4_name((struct sockaddr_in*)res->ai_addr, addr, 16);
	iinfo->dest = (struct sockaddr_in*)res->ai_addr;
	iinfo->key = malloc(64);
	snprintf(iinfo->key, 64, "%s:%u:%d", addr, iinfo->dest->sin_port, iinfo->dest->sin_family);
	printf("resolved %s\n", iinfo->key);

	tommy_hashdyn_insert(ac->iggregator, &(iinfo->node), iinfo, tommy_strhash_u32(0, iinfo->key));
}

void do_icmp_client(char *addr)
{
	extern aconf* ac;
	uv_loop_t *loop = ac->loop;
	icmp_info *iinfo = calloc(1, sizeof(*iinfo));

	uv_getaddrinfo_t *resolver = malloc(sizeof(*resolver));
	iinfo->address = addr;
	resolver->data = iinfo;
	int r = uv_getaddrinfo(loop, resolver, on_resolved, addr, 0, NULL);
	if (r)
	{
		fprintf(stderr, "%s\n", uv_strerror(r));
		return;
	}
	else
	{
		(ac->icmp_client_count)++;
	}
}

//int main(int argc, char **argv)
//{
//	int i=1;
//	for (i=1; i<argc; i++)
//	{
//		do_icmp(argv[i]);
//	}
//}
#endif
