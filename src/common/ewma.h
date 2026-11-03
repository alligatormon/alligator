#pragma once
#include <math.h>
#include <stdint.h>

/* One EWMA step. Non-positive dt/tau or non-finite prev seeds to sample. */
static inline double ewma_step(double prev, double sample, double dt_sec, double tau_sec)
{
	double alpha;

	if (!(dt_sec > 0.0) || !(tau_sec > 0.0) || !isfinite(prev))
		return sample;
	alpha = exp(-dt_sec / tau_sec);
	return prev * alpha + sample * (1.0 - alpha);
}

/* Lateness of a periodic timer tick in seconds (0 when on time or early). */
static inline double loop_delay_seconds(uint64_t elapsed_ns, uint64_t expected_ns)
{
	if (elapsed_ns <= expected_ns)
		return 0.0;
	return (double)(elapsed_ns - expected_ns) / 1e9;
}

/* adjtimex offset: microseconds, or nanoseconds when STA_NANO is set. */
static inline double timex_offset_to_seconds(long offset, int status_nano)
{
	if (status_nano)
		return (double)offset / 1e9;
	return (double)offset / 1e6;
}
