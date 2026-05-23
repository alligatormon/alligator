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

static int64_t rtime_delta_ns_signed(r_time t1, r_time t2)
{
	if ((!t1.sec && !t1.nsec) || (!t2.sec && !t2.nsec))
		return 0;

	int64_t sec_delta = (int64_t)t2.sec - (int64_t)t1.sec;
	int64_t nsec_delta = (int64_t)t2.nsec - (int64_t)t1.nsec;
	return sec_delta * 1000000000LL + nsec_delta;
}

void getrtime(r_time t1, r_time t2)
{
	int64_t delta_ns = rtime_delta_ns_signed(t1, t2);
	double delta_sec = (double)delta_ns / 1000000000.0;
	printf("complete for: %.09f sec\n", delta_sec);
}

uint64_t getrtime_ns(r_time t1, r_time t2)
{
	int64_t delta_ns = rtime_delta_ns_signed(t1, t2);
	if (delta_ns <= 0)
		return 0;
	return (uint64_t)delta_ns;
}

uint64_t getrtime_mcs(r_time t1, r_time t2, int debug)
{
	int64_t delta_ns = rtime_delta_ns_signed(t1, t2);
	uint64_t ret = (delta_ns <= 0) ? 0 : (uint64_t)(delta_ns / 1000);
	if (debug)
		printf("complete for: %.09f sec (%d - %d sec, %d - %d nsec), ret: %"u64"\n",
			   (double)delta_ns / 1000000000.0, t2.sec, t1.sec, t2.nsec, t1.nsec, ret);
	return ret;
}

uint64_t getrtime_ms(r_time t1, r_time t2)
{
	int64_t delta_ns = rtime_delta_ns_signed(t1, t2);
	if (delta_ns <= 0)
		return 0;
	return (uint64_t)(delta_ns / 1000000);
}

double getrtime_msec_float(r_time t2, r_time t1)
{
	return (double)rtime_delta_ns_signed(t1, t2) / 1000000.0;
}

double getrtime_sec_float(r_time t2, r_time t1)
{
	return (double)rtime_delta_ns_signed(t1, t2) / 1000000000.0;
}

uint64_t getrtime_now_ms(r_time t1)
{
	uint64_t ret = ((t1.sec)*1000 + ((t1.nsec)/1000000));
	return ret;
}

uint64_t getrtime_elapsed_ms(r_time start, r_time end)
{
	return getrtime_ms(start, end);
}
