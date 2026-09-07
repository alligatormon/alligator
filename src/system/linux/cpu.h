#pragma once
#include <inttypes.h>

int get_scaling_current_cpu_freq();
void get_cpu_avg();
void get_cpu(int8_t platform);
void cpu_avg_push(double now);
#ifdef __linux__
void get_linux_cpuidle(void);
#endif