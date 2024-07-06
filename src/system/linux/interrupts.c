#ifdef __linux__
#include "main.h"
#include "common/logs.h"
extern aconf *ac;
void get_proc_interrupts(int extended_mode)
{
	char interdir[255];
	snprintf(interdir, 255, "%s/interrupts", ac->system_procfs);
	carglog(ac->system_carg, L_INFO, "system scrape metrics: disk: get_proc_interrupts: %s, extended mode: %d\n", interdir, extended_mode);
	FILE *fd = fopen(interdir, "r");
	if (!fd)
		return;

	char buf[4096];
	char field[1024];
	char code[16] = { 0 };
	char description[1024] = { 0 };
	uint16_t cpus = 0;
	uint16_t columns = 0;
	uint64_t data = 0;
	uint64_t *values = NULL;;
	while (fgets(buf, 4095, fd)) {
		char *cur = buf + strspn(buf, " \t");
		carglog(ac->system_carg, L_TRACE, "\tget_proc_interrupts: parse '%s'\n", buf);
		uint64_t i = 0;
		for (i = 0; *cur; ++i) {
			uint64_t size = strcspn_n(cur, " \t\n", 1023);
			strlcpy(field, cur, size+1);
			if (!data) {
				if (!strncmp(field, "CPU", 3))
					++cpus;
			}
			else if (!i) {
				strlcpy(code, field, size);
				carglog(ac->system_carg, L_TRACE, "\t\tget_proc_interrupts: set code to [%lu]: '%s'\n", i, code);
			}
			else if (i < columns) {
				carglog(ac->system_carg, L_TRACE, "\t\tget_proc_interrupts: parsed [%lu]: '%s'\n", i, field);
				values[i-1] = strtoull(field, NULL, 10);
			}
			else {
				break;
			}

			cur += size + strspn(cur+size, " \t\n");
		}
		size_t copy_size = strlcpy(description, cur, 1024);
		description[copy_size-1] = 0;

		uint64_t value = 0;
		--i;
		if (data) {
			for (uint64_t j = 0; j < i; ++j) {
				value += values[j];
				if (extended_mode && data) {
					carglog(ac->system_carg, L_TRACE, "\t\tget_proc_interrupts values cpu[%lu]: '%lu' ", j, values[j]);
					char cpuname[26];
					snprintf(cpuname, 25, "CPU%"u64, j);
					metric_add_labels3("interrupts_core_count", &values[j], DATATYPE_UINT, ac->system_carg, "code", code, "description", description, "cpu", cpuname);
				}
			}
		}

		if (!data) {
			data = 1;
			columns = cpus + 1;
			values = calloc(1, sizeof(*values) * cpus);
		}
		else
			metric_add_labels2("interrupts_count", &value, DATATYPE_UINT, ac->system_carg, "code", code, "description", description);
	}
	free(values);
	fclose(fd);
}
#endif
