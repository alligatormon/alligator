#ifdef __linux__

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "main.h"
#include "common/logs.h"
#include "system/linux/pressure.h"

extern aconf *ac;

static void emit_pressure_avgs(char *metric, char *resource, double avg10, double avg60, double avg300)
{
	metric_add_labels2(metric, &avg10, DATATYPE_DOUBLE, ac->system_carg,
		"resource", resource, "window", "10");
	metric_add_labels2(metric, &avg60, DATATYPE_DOUBLE, ac->system_carg,
		"resource", resource, "window", "60");
	metric_add_labels2(metric, &avg300, DATATYPE_DOUBLE, ac->system_carg,
		"resource", resource, "window", "300");
}

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
		if (!pressure_parse_line(line, kind, sizeof(kind), &avg10, &avg60, &avg300, &total))
			continue;

		double total_seconds = total / 1000000.0;
		if (!strcmp(kind, "some")) {
			metric_add_labels("pressure_waiting_seconds_total", &total_seconds, DATATYPE_DOUBLE,
				ac->system_carg, "resource", resource);
			emit_pressure_avgs("pressure_waiting_avg_percent", resource, avg10, avg60, avg300);
		} else if (!strcmp(kind, "full")) {
			metric_add_labels("pressure_stalled_seconds_total", &total_seconds, DATATYPE_DOUBLE,
				ac->system_carg, "resource", resource);
			emit_pressure_avgs("pressure_stalled_avg_percent", resource, avg10, avg60, avg300);
		}
	}
	fclose(fd);
}

int pressure_parse_line(const char *line, char *kind, size_t kind_cap,
	double *avg10, double *avg60, double *avg300, uint64_t *total)
{
	char tmp[16];
	double a10, a60, a300;
	uint64_t tot;
	int n;

	if (!line || !kind || !kind_cap || !avg10 || !avg60 || !avg300 || !total)
		return 0;
	n = sscanf(line, "%15s avg10=%lf avg60=%lf avg300=%lf total=%" SCNu64,
		tmp, &a10, &a60, &a300, &tot);
	if (n < 5)
		return 0;
	snprintf(kind, kind_cap, "%s", tmp);
	*avg10 = a10;
	*avg60 = a60;
	*avg300 = a300;
	*total = tot;
	return 1;
}

void get_pressure_stats(void)
{
	parse_pressure_file("cpu");
	parse_pressure_file("memory");
	parse_pressure_file("io");
	parse_pressure_file("irq");
}

#endif
