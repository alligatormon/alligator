#pragma once

#ifdef __linux__

void get_softnet_stats(void);
void get_sockstat_stats(void);
void get_bonding_stats(void);
void get_arp_stats(void);
void get_ipvs_stats(void);
void get_snmp6_stats(void);
void get_synproxy_stats(void);
void get_wireguard_stats(void);
void get_wireless_stats(void);

#endif
