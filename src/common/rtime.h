#pragma once
#include <time.h>
#include <inttypes.h>
typedef struct r_time
{
	int sec;
	int nsec;
} r_time;

r_time setrtime();
uint64_t getrtime_mcs(r_time t1, r_time t2, int debug);
uint64_t getrtime_now_ms(r_time t1);
double getrtime_sec_float(r_time t2, r_time t1);
double getrtime_msec_float(r_time t2, r_time t1);
uint64_t getrtime_ns(r_time t1, r_time t2);
