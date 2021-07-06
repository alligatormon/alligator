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
