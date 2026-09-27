#include "cadvisor/parsers.h"
#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

double cadvisor_ns_to_seconds(uint64_t ns)
{
	return (double)ns / 1000000000.0;
}

double cadvisor_usec_to_seconds(uint64_t usec)
{
	return (double)usec / 1000000.0;
}

long cadvisor_clk_tck(void)
{
	long hz = sysconf(_SC_CLK_TCK);
	if (hz <= 0)
		return 100;
	return hz;
}

double cadvisor_ticks_to_seconds(uint64_t ticks, long hz)
{
	if (hz <= 0)
		hz = 100;
	return (double)ticks / (double)hz;
}

static int numa_type_scope(const char *token, int cgroup_v2, const char **type, const char **scope)
{
	if (!token || !type || !scope)
		return 0;

	if (!strcmp(token, "file")) {
		*type = "file";
		*scope = "container";
		return 1;
	}
	if (!strcmp(token, "anon")) {
		*type = "anon";
		*scope = "container";
		return 1;
	}
	if (!strcmp(token, "unevictable")) {
		*type = "unevictable";
		*scope = "container";
		return 1;
	}
	if (cgroup_v2)
		return 0;

	if (!strcmp(token, "total_file")) {
		*type = "file";
		*scope = "hierarchy";
		return 1;
	}
	if (!strcmp(token, "total_anon")) {
		*type = "anon";
		*scope = "hierarchy";
		return 1;
	}
	if (!strcmp(token, "total_unevictable")) {
		*type = "unevictable";
		*scope = "hierarchy";
		return 1;
	}
	return 0;
}

int cadvisor_parse_numa_stat_line(const char *line, int cgroup_v2, cadvisor_numa_cb cb, void *arg)
{
	char buf[1024];
	char *save = NULL;
	char *tok;
	const char *type = NULL;
	const char *scope = NULL;
	int emitted = 0;

	if (!line || !cb)
		return 0;

	strlcpy(buf, line, sizeof(buf));
	tok = strtok_r(buf, " \t\n", &save);
	if (!tok)
		return 0;
	if (!numa_type_scope(tok, cgroup_v2, &type, &scope))
		return 0;

	while ((tok = strtok_r(NULL, " \t\n", &save))) {
		char node[32];
		char *eq = strchr(tok, '=');
		uint64_t pages;

		if (!eq || eq == tok || strncmp(tok, "N", 1))
			continue;
		if ((size_t)(eq - tok) >= sizeof(node))
			continue;
		memcpy(node, tok, (size_t)(eq - tok));
		node[eq - tok] = '\0';
		pages = strtoull(eq + 1, NULL, 10);
		cb(type, scope, node, pages, arg);
		emitted = 1;
	}
	return emitted;
}

int cadvisor_parse_pressure_line(const char *line, char *kind, size_t kindsz,
	double *avg10, double *avg60, double *avg300, uint64_t *total_usec)
{
	char k[16];
	double a10, a60, a300;
	uint64_t total;
	int n;

	if (!line || !kind || !avg10 || !avg60 || !avg300 || !total_usec || kindsz < 2)
		return 0;

	n = sscanf(line, "%15s avg10=%lf avg60=%lf avg300=%lf total=%" SCNu64,
		k, &a10, &a60, &a300, &total);
	if (n < 5)
		return 0;

	strlcpy(kind, k, kindsz);
	*avg10 = a10;
	*avg60 = a60;
	*avg300 = a300;
	*total_usec = total;
	return 1;
}

int cadvisor_parse_snmp_tcp_pair(const char *header_line, const char *value_line,
	cadvisor_snmp_tcp_cb cb, void *arg)
{
	char hbuf[2048];
	char vbuf[2048];
	char *hsave = NULL;
	char *vsave = NULL;
	char *htok;
	char *vtok;
	int emitted = 0;

	if (!header_line || !value_line || !cb)
		return 0;
	if (strncmp(header_line, "Tcp:", 4) || strncmp(value_line, "Tcp:", 4))
		return 0;

	strlcpy(hbuf, header_line + 4, sizeof(hbuf));
	strlcpy(vbuf, value_line + 4, sizeof(vbuf));

	htok = strtok_r(hbuf, " \t\n", &hsave);
	vtok = strtok_r(vbuf, " \t\n", &vsave);
	while (htok && vtok) {
		char field[64];
		size_t i;
		uint64_t val = strtoull(vtok, NULL, 10);

		for (i = 0; htok[i] && i + 1 < sizeof(field); ++i)
			field[i] = (char)tolower((unsigned char)htok[i]);
		field[i] = '\0';
		cb(field, val, arg);
		emitted = 1;
		htok = strtok_r(NULL, " \t\n", &hsave);
		vtok = strtok_r(NULL, " \t\n", &vsave);
	}
	return emitted;
}

const char *cadvisor_io_cost_metric_name(const char *key)
{
	if (!key)
		return NULL;
	if (!strncmp(key, "cost.usage", 10))
		return "container_fs_io_cost_usage_seconds_total";
	if (!strncmp(key, "cost.wait", 9))
		return "container_fs_io_cost_wait_seconds_total";
	if (!strncmp(key, "cost.indebt", 11))
		return "container_fs_io_cost_indebt_seconds_total";
	if (!strncmp(key, "cost.indelay", 12))
		return "container_fs_io_cost_indelay_seconds_total";
	return NULL;
}

const char *cadvisor_memory_events_metric_name(const char *key)
{
	if (!key)
		return NULL;
	/* oom_group_kill and oom_kill before the oom prefix */
	if (!strncmp(key, "oom_group_kill", 14))
		return "container_memory_events_oom_group_kill_total";
	if (!strncmp(key, "oom_kill", 8))
		return "container_memory_oom_kill";
	if (!strncmp(key, "oom", 3))
		return "container_oom_events_total";
	if (!strncmp(key, "low", 3))
		return "container_memory_events_low_total";
	if (!strncmp(key, "high", 4))
		return "container_memory_events_high_total";
	if (!strncmp(key, "max", 3))
		return "container_memory_events_max_total";
	return NULL;
}

int cadvisor_overlay_upperdir(const char *opts, char *out, size_t outsz)
{
	const char *p;
	size_t n;

	if (!opts || !out || outsz < 2)
		return 0;
	p = strstr(opts, "upperdir=");
	if (!p)
		return 0;
	p += strlen("upperdir=");
	n = strcspn(p, ", \t\n");
	if (n == 0 || n >= outsz)
		return 0;
	memcpy(out, p, n);
	out[n] = '\0';
	return 1;
}

int cadvisor_parse_perf_event_config(const char *text, uint64_t *config)
{
	char buf[512];
	char *save = NULL;
	char *tok;
	size_t n;
	int have = 0;
	int have_raw = 0;
	uint64_t event = 0, umask = 0, cmask = 0, raw = 0;

	if (!text || !config)
		return 0;
	n = 0;
	while (text[n] && n + 1 < sizeof(buf)) {
		buf[n] = text[n];
		++n;
	}
	buf[n] = '\0';

	for (tok = strtok_r(buf, ", \t\r\n", &save); tok; tok = strtok_r(NULL, ", \t\r\n", &save)) {
		if (!strncmp(tok, "config=", 7)) {
			raw = strtoull(tok + 7, NULL, 0);
			have_raw = 1;
			have = 1;
		} else if (!strncmp(tok, "event=", 6)) {
			event = strtoull(tok + 6, NULL, 0);
			have = 1;
		} else if (!strncmp(tok, "umask=", 6)) {
			umask = strtoull(tok + 6, NULL, 0);
			have = 1;
		} else if (!strncmp(tok, "cmask=", 6)) {
			cmask = strtoull(tok + 6, NULL, 0);
			have = 1;
		}
	}
	if (!have)
		return 0;
	if (have_raw)
		*config = raw;
	else
		*config = event | (umask << 8) | (cmask << 24);
	return 1;
}

int cadvisor_dir_has_entries(const char *path)
{
	DIR *dp;
	struct dirent *entry;
	int found = 0;

	if (!path)
		return 0;
	dp = opendir(path);
	if (!dp)
		return 0;
	while ((entry = readdir(dp))) {
		if (entry->d_name[0] == '.')
			continue;
		found = 1;
		break;
	}
	closedir(dp);
	return found;
}
