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
#ifndef _WIN32 //win32
#ifdef __MACH__ //mach
	clock_serv_t cclock;
	mach_timespec_t timer1;
	host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
	clock_get_time(cclock, &timer1);
	mach_port_deallocate(mach_task_self(), cclock);

#else //mach
	struct timespec timer1;
	clock_gettime(CLOCK_REALTIME, &timer1);
#endif //mach
	r_time rt;
	rt.sec=timer1.tv_sec;
	rt.nsec=timer1.tv_nsec;
	return rt;
#else // win32
	r_time rt = {0, 0};
	return rt;
#endif
}

void getrtime(r_time t1, r_time t2)
{
	printf("complete for: %u.%09d sec\n",t2.sec-t1.sec,t2.nsec-t1.nsec);
}

int64_t getrtime_i(r_time t1, r_time t2)
{
	int64_t ret = (t2.sec-t1.sec)*1000000000 + ((t2.nsec-t1.nsec));
	return ret;
}
