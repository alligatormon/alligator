#ifdef __FreeBSD__
#include "main.h"
#include "system/freebsd/parsers.h"
#include "common/selector.h"
#include "common/logs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libproc.h>

extern aconf *ac;

int freebsd_scrape_pid(pid_t pid);

static void normalize_service_name(char *name)
{
	size_t len;

	if (!name)
		return;

	len = strlen(name);
	if (len > 8 && !strcmp(name + len - 8, ".service"))
		name[len - 8] = '\0';
}

static int service_user_allowed(const char *username)
{
	uint32_t hash;

	if (!ac->system_services_checking_users || !alligator_ht_count(ac->system_services_checking_users))
		return 1;
	if (!username)
		return 0;

	hash = tommy_strhash_u32(0, username);
	return alligator_ht_search(ac->system_services_checking_users, system_string_compare,
		(void *)username, hash) != NULL;
}

static int rc_conf_enabled(const char *name, const char *path)
{
	FILE *fd;
	char line[1024];
	char key[256];
	size_t key_len;

	snprintf(key, sizeof(key), "%s_enable", name);
	key_len = strlen(key);

	fd = fopen(path, "r");
	if (!fd)
		return 0;

	while (fgets(line, sizeof(line), fd)) {
		char *p = line;

		while (*p == ' ' || *p == '\t')
			++p;
		if (*p == '#')
			continue;
		if (strncmp(p, key, key_len) != 0)
			continue;
		if (strcasestr(p, "YES")) {
			fclose(fd);
			return 1;
		}
	}

	fclose(fd);
	return 0;
}

static uint64_t rc_service_enabled(const char *name)
{
	static const char *paths[] = {
		"/etc/rc.conf",
		"/etc/rc.conf.local",
		"/usr/local/etc/rc.conf",
		NULL
	};
	size_t i;

	for (i = 0; paths[i]; ++i) {
		if (rc_conf_enabled(name, paths[i]))
			return 1;
	}
	return 0;
}

static pid_t read_pidfile(const char *path)
{
	FILE *fd;
	char buf[32];
	pid_t pid = -1;

	fd = fopen(path, "r");
	if (!fd)
		return -1;

	if (fgets(buf, sizeof(buf), fd)) {
		pid = (pid_t)atoi(buf);
		if (pid <= 0)
			pid = -1;
	}
	fclose(fd);
	return pid;
}

static pid_t rc_service_pid(const char *name)
{
	char pidfile[PATH_MAX];
	pid_t pid;

	snprintf(pidfile, sizeof(pidfile), "/var/run/%s.pid", name);
	pid = read_pidfile(pidfile);
	if (pid > 0 && kill(pid, 0) == 0)
		return pid;

	snprintf(pidfile, sizeof(pidfile), "/var/run/%s/%s.pid", name, name);
	pid = read_pidfile(pidfile);
	if (pid > 0 && kill(pid, 0) == 0)
		return pid;

	return -1;
}

static uint64_t rc_service_running(const char *name)
{
	return rc_service_pid(name) > 0 ? 1 : 0;
}

static void get_service_tasks_status(const char *servicename, const char *type, const char *username)
{
	pid_t pid = rc_service_pid(servicename);
	uint64_t cnt = 0;
	int cntp = 0, i;
	struct kinfo_proc *proc;

	if (pid <= 0) {
		metric_add_labels3("service_tasks_count", &cnt, DATATYPE_UINT, ac->system_carg,
			"service", servicename, "type", type, "username", username);
		return;
	}

	if (!strcmp(type, "threads")) {
		proc = kinfo_getallproc(&cntp);
		if (proc) {
			for (i = 0; i < cntp; i++) {
				if (proc[i].ki_pid == pid) {
					cnt = proc[i].ki_numthreads;
					break;
				}
			}
			free(proc);
		}
	} else {
		cnt = 1;
	}

	metric_add_labels3("service_tasks_count", &cnt, DATATYPE_UINT, ac->system_carg,
		"service", servicename, "type", type, "username", username);
}

static void service_running_status(char *name, char *username)
{
	char normalized[256];
	int has_services;
	int has_services_process;
	uint64_t running;
	uint64_t enabled;
	pid_t pid;

	strlcpy(normalized, name, sizeof(normalized));
	normalize_service_name(normalized);

	has_services = (match_mapper(ac->services_match, normalized, strlen(normalized), normalized) == 1);
	has_services_process = (match_mapper(ac->services_process_match, normalized, strlen(normalized), normalized) == 1);

	if (!has_services && !has_services_process)
		return;

	enabled = rc_service_enabled(normalized);
	running = rc_service_running(normalized);

	metric_add_labels2("service_enabled", &enabled, DATATYPE_UINT, ac->system_carg,
		"service", normalized, "username", username ? username : "system");
	metric_add_labels2("service_running", &running, DATATYPE_UINT, ac->system_carg,
		"service", normalized, "username", username ? username : "system");

	get_service_tasks_status(normalized, "processes", username ? username : "system");
	get_service_tasks_status(normalized, "threads", username ? username : "system");

	if (has_services || has_services_process)
		metric_add_labels2("service_match", &running, DATATYPE_UINT, ac->system_carg,
			"service", normalized, "username", username ? username : "system");

	if (has_services_process && running) {
		pid = rc_service_pid(normalized);
		if (pid > 0)
			freebsd_scrape_pid(pid);
	}
}

static int service_name_seen(char **seen, size_t seen_count, const char *name, const char *username)
{
	char key[2048];
	size_t i;

	snprintf(key, sizeof(key), "%s|%s", username ? username : "system", name ? name : "");
	for (i = 0; i < seen_count; ++i) {
		if (!strcmp(seen[i], key))
			return 1;
	}
	return 0;
}

static int service_name_seen_any_user(char **seen, size_t seen_count, const char *name)
{
	size_t i, name_len;

	if (!name)
		return 0;

	name_len = strlen(name);
	for (i = 0; i < seen_count; ++i) {
		size_t seen_len = strlen(seen[i]);
		if (seen_len > name_len + 1 && !strcmp(seen[i] + seen_len - name_len, name)
			&& seen[i][seen_len - name_len - 1] == '|')
			return 1;
	}
	return 0;
}

static void service_name_add(char ***seen, size_t *seen_count, size_t *seen_cap,
	const char *name, const char *username)
{
	char key[2048];
	char *entry;

	snprintf(key, sizeof(key), "%s|%s", username ? username : "system", name ? name : "");

	if (*seen_count == *seen_cap) {
		size_t new_cap = (*seen_cap == 0) ? 64 : (*seen_cap * 2);
		char **new_seen = realloc(*seen, new_cap * sizeof(*new_seen));
		if (!new_seen)
			return;
		*seen = new_seen;
		*seen_cap = new_cap;
	}

	entry = strdup(key);
	if (!entry)
		return;
	(*seen)[*seen_count] = entry;
	++(*seen_count);
}

static int is_rc_script(const char *path, const char *name)
{
	struct stat st;

	if (!name || name[0] == '.')
		return 0;
	if (strstr(name, ".sh") == name + strlen(name) - 3)
		return 0;

	if (stat(path, &st) != 0)
		return 0;
	return S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR);
}

static void scan_rc_dir_unique(const char *path, char *username,
	char ***seen, size_t *seen_count, size_t *seen_cap)
{
	DIR *dp;
	struct dirent *entry;
	char fullpath[PATH_MAX];

	dp = opendir(path);
	if (!dp)
		return;

	while ((entry = readdir(dp))) {
		if (entry->d_name[0] == '.')
			continue;

		snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
		if (!is_rc_script(fullpath, entry->d_name))
			continue;
		if (service_name_seen(*seen, *seen_count, entry->d_name, username))
			continue;

		service_running_status(entry->d_name, username);
		service_name_add(seen, seen_count, seen_cap, entry->d_name, username);
	}

	closedir(dp);
}

typedef struct service_not_found_emit_ctx {
	char ***seen;
	size_t *seen_count;
	size_t *seen_cap;
} service_not_found_emit_ctx;

static void emit_not_found_service_match_cb(void *arg, void *obj)
{
	service_not_found_emit_ctx *ctx = arg;
	match_string *ms = obj;
	uint64_t val = 2;

	if (!ctx || !ms || !ms->s || !ms->s[0])
		return;

	if (!service_name_seen_any_user(*ctx->seen, *ctx->seen_count, ms->s)) {
		metric_add_labels2("service_match", &val, DATATYPE_UINT, ac->system_carg,
			"service", ms->s, "username", "not_found");
		service_name_add(ctx->seen, ctx->seen_count, ctx->seen_cap, ms->s, "not_found");
	}
}

static void emit_not_found_service_match(alligator_ht *hash, char ***seen,
	size_t *seen_count, size_t *seen_cap)
{
	service_not_found_emit_ctx ctx = { seen, seen_count, seen_cap };

	if (!hash)
		return;
	alligator_ht_foreach_arg(hash, emit_not_found_service_match_cb, &ctx);
}

void get_services(void)
{
	char **seen_services = NULL;
	size_t seen_count = 0;
	size_t seen_cap = 0;
	uint8_t has_service_user_filter = (ac->system_services_checking_users
		&& alligator_ht_count(ac->system_services_checking_users));
	size_t i;

	if (!has_service_user_filter || service_user_allowed("system")) {
		scan_rc_dir_unique("/etc/rc.d", "system", &seen_services, &seen_count, &seen_cap);
		scan_rc_dir_unique("/usr/local/etc/rc.d", "system", &seen_services, &seen_count, &seen_cap);
	}

	emit_not_found_service_match(ac->services_match ? ac->services_match->hash : NULL,
		&seen_services, &seen_count, &seen_cap);
	emit_not_found_service_match(ac->services_process_match ? ac->services_process_match->hash : NULL,
		&seen_services, &seen_count, &seen_cap);

	for (i = 0; i < seen_count; ++i)
		free(seen_services[i]);
	free(seen_services);
}
#endif
