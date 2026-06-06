#pragma once

#ifdef __linux__

#include <stdint.h>
#include <net/if.h>
#include "events/context_arg.h"

typedef struct network_if_stats {
	char ifname[IFNAMSIZ];
	uint64_t rx_bytes;
	uint64_t rx_packets;
	uint64_t rx_errors;
	uint64_t rx_dropped;
	uint64_t rx_fifo_errors;
	uint64_t rx_frame_errors;
	uint64_t rx_compressed;
	uint64_t multicast;
	uint64_t tx_bytes;
	uint64_t tx_packets;
	uint64_t tx_errors;
	uint64_t tx_dropped;
	uint64_t tx_fifo_errors;
	uint64_t collisions;
	uint64_t tx_carrier_errors;
	uint64_t tx_compressed;
	int64_t speed;
	uint8_t valid;
} network_if_stats;

void network_if_stats_reset(network_if_stats *st);

typedef int (*network_if_stats_cb)(const network_if_stats *st, void *arg);
/* cb return non-zero stops enumeration */
/* sysfs used only for /class/net/<if>/speed when IFLA_SPEED is missing or zero */
int network_netlink_foreach(network_if_stats_cb cb, void *arg, uint8_t skip_veth, context_arg *log_carg, const char *sysfs);

int network_if_stats_netlink(const char *ifname, network_if_stats *st);

void network_if_stats_emit_system(const network_if_stats *st, context_arg *carg);

void get_network_statistics_netlink();

#endif
