#pragma once

#ifdef __linux__

void get_swap_stats(void);
void get_schedstat_stats(void);
void get_slabinfo_stats(void);

#endif
