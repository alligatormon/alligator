#pragma once
#include <time.h>
typedef struct r_time
{
	int sec;
	int nsec;
} r_time;

r_time setrtime();
