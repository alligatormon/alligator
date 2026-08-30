#pragma once
#ifdef __linux__
#include <stdint.h>

void find_pid(int8_t lightweight);
void get_pidfile_stats();
void get_userprocess_stats();
void cgroup_procs_scrape(char *cgroup_path);
void clear_counts_process();
void fill_counts_process();
long linux_user_hz(void);
int userprocess_compare(const void *arg, const void *obj);
int system_string_compare(const void *arg, const void *obj);
#endif
