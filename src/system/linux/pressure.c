#ifdef __linux__

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "main.h"
#include "common/logs.h"
#include "system/linux/pressure.h"

extern aconf *ac;

static void parse_pressure_file(char *resource)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/pressure/%s", ac->system_procfs, resource);
	carglog(ac->system_carg, L_TRACE, "system scrape metrics: base: pressure '%s'\n", path);

	FILE *fd = fopen(path, "r");
	if (!fd)
		return;

	char line[256];
	while (fgets(line, sizeof(line), fd)) {
		char kind[16];
		double avg10, avg60, avg300;
		uint64_t total;
		int n = sscanf(line, "%15s avg10=%lf avg60=%lf avg300=%lf total=%" SCNu64,
			kind, &avg10, &avg60, &avg300, &total);
		if (n < 5)
			continue;

		double total_seconds = total / 1000000.0;
		if (!strcmp(kind, "some")) {
			metric_add_labels("pressure_waiting_seconds_total", &total_seconds, DATATYPE_DOUBLE,
				ac->system_carg, "resource", resource);
		} else if (!strcmp(kind, "full")) {
			metric_add_labels("pressure_stalled_seconds_total", &total_seconds, DATATYPE_DOUBLE,
				ac->system_carg, "resource", resource);
		}
	}
	fclose(fd);
}

void get_pressure_stats(void)
{
	parse_pressure_file("cpu");
	parse_pressure_file("memory");
	parse_pressure_file("io");
}

#endif
