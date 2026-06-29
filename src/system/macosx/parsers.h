#pragma once
#include <sys/types.h>

int8_t get_platform(int8_t mode);
void get_kernel_version(int8_t platform);
void get_distribution_name(void);
void get_memory_usage_hw(void);

void get_proc_info(int8_t lightweight);
void get_services(void);
void get_pidfile_stats(void);
void get_userprocess_stats(void);

int userprocess_compare(const void *arg, const void *obj);
int system_string_compare(const void *arg, const void *obj);
int macosx_scrape_pid(pid_t pid);

void get_network_statistics(void);
void check_sockets_macos(void);
void disk_io_stats(void);
void disks_info(void);
void bsd_firewall_collect(void);
