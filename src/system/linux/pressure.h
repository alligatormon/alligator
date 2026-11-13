#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __linux__

int pressure_parse_line(const char *line, char *kind, size_t kind_cap,
	double *avg10, double *avg60, double *avg300, uint64_t *total);
void get_pressure_stats(void);

#endif
