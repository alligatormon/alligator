#include <stdio.h>
#include "rtime.h"
#include <inttypes.h>
#define d64 PRId64
#define u64 PRIu64
#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

r_time setrtime()
{
	struct timespec timer1;
	int ret = clock_gettime(CLOCK_REALTIME, &timer1);
	r_time rt = {0, 0};
	if (ret != -1) {
		rt.sec=timer1.tv_sec;
		rt.nsec=timer1.tv_nsec;
	}
	return rt;
}

void getrtime(r_time t1, r_time t2)
{
	printf("complete for: %u.%09d sec\n",t2.sec-t1.sec,t2.nsec-t1.nsec);
}

uint64_t getrtime_ns(r_time t1, r_time t2)
{
	uint64_t ret = (t2.sec-t1.sec)*1000000000 + ((t2.nsec-t1.nsec));
	return ret;
}

uint64_t getrtime_mcs(r_time t1, r_time t2, int debug)
{
	uint64_t ret = ((t2.sec-t1.sec)*1000000 + ((t2.nsec-t1.nsec)/1000));
	if (debug)
		printf("complete for: %u.%09d sec (%d - %d sec, %d - %d nsec), ret: %"u64"\n",t2.sec-t1.sec,t2.nsec-t1.nsec, t2.sec, t1.sec, t2.nsec, t1.nsec, ret);
	return ret;
}

uint64_t getrtime_ms(r_time t1, r_time t2)
{
	uint64_t ret = ((t2.sec-t1.sec)*1000.0 + ((t2.nsec-t1.nsec)/1000000.0));
	//printf("complete for: %u.%09d sec, ret: %llu\n",t2.sec-t1.sec,t2.nsec-t1.nsec, ret);
	return ret;
}

double getrtime_sec_float(r_time t2, r_time t1)
{
	double ret = ((t2.sec-t1.sec)*1000 + ((t2.nsec-t1.nsec)/1000000));
	return ret;
}

uint64_t getrtime_now_ms(r_time t1)
{
	uint64_t ret = ((t1.sec)*1000 + ((t1.nsec)/1000000));
	return ret;
}
