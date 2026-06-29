#ifdef __APPLE__
#include "main.h"
#include "system/macosx/parsers.h"
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

static void normalize_service_name(char *name)
{
	size_t len;

	if (!name)
		return;

	len = strlen(name);
	if (len > 8 && !strcmp(name + len - 8, ".service"))
		name[len - 8] = '\0';
	if (len > 5 && !strcmp(name + len - 5, ".plist"))
		name[len - 5] = '\0';
}

static int launchd_plist_enabled(const char *label, const char *dir)
{
	char path[PATH_MAX];
	FILE *fd;
	char line[1024];

	snprintf(path, sizeof(path), "%s/%s.plist", dir, label);
	if (access(path, R_OK) != 0 && strchr(label, '.') == NULL) {
		snprintf(path, sizeof(path), "%s/%s", dir, label);
		if (access(path, R_OK) != 0)
			return 0;
	}

	fd = fopen(path, "r");
	if (!fd)
		return 1;

	while (fgets(line, sizeof(line), fd)) {
		if (strstr(line, "<key>Disabled</key>")) {
			char next[64];
			if (fgets(next, sizeof(next), fd) && strstr(next, "<true/>")) {
				fclose(fd);
				return 0;
			}
		}
	}
	fclose(fd);
	return 1;
}

static uint64_t launchd_service_enabled(const char *name)
{
	if (launchd_plist_enabled(name, "/Library/LaunchDaemons"))
		return 1;
	if (launchd_plist_enabled(name, "/Library/LaunchAgents"))
		return 1;
	return 0;
}

static pid_t launchd_service_pid(const char *name)
{
	char cmd[512];
	FILE *fp;
	char line[512];
	pid_t pid = -1;

	snprintf(cmd, sizeof(cmd), "launchctl list 2>/dev/null | awk '$3==\"%s\" {print $1}'", name);
	fp = popen(cmd, "r");
	if (!fp)
		return -1;
	if (fgets(line, sizeof(line), fp)) {
		pid = (pid_t)atoi(line);
		if (pid <= 0)
			pid = -1;
	}
	pclose(fp);
	return pid;
}

static uint64_t launchd_service_running(const char *name)
{
	pid_t pid = launchd_service_pid(name);
	return (pid > 0 && kill(pid, 0) == 0) ? 1 : 0;
}

static void get_service_tasks_status(const char *servicename, const char *type, const char *username)
{
	pid_t pid = launchd_service_pid(servicename);
	uint64_t cnt = 0;
	struct proc_taskinfo task_info;

	if (pid <= 0) {
		metric_add_labels3("service_tasks_count", &cnt, DATATYPE_UINT, ac->system_carg,
			"service", (char *)servicename, "type", (char *)type, "username", (char *)username);
		return;
	}

	if (!strcmp(type, "threads")
	    && proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &task_info, PROC_PIDTASKINFO_SIZE) == PROC_PIDTASKINFO_SIZE)
		cnt = task_info.pti_threadnum;
	else
		cnt = 1;

	metric_add_labels3("service_tasks_count", &cnt, DATATYPE_UINT, ac->system_carg,
		"service", (char *)servicename, "type", (char *)type, "username", (char *)username);
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

	enabled = launchd_service_enabled(normalized);
	running = launchd_service_running(normalized);

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
		pid = launchd_service_pid(normalized);
		if (pid > 0)
			macosx_scrape_pid(pid);
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

static int is_launchd_plist(const char *name)
{
	size_t len;

	if (!name || name[0] == '.')
		return 0;
	len = strlen(name);
	return len > 6 && !strcmp(name + len - 6, ".plist");
}

static void scan_launchd_dir(const char *path, char *username,
	char ***seen, size_t *seen_count, size_t *seen_cap)
{
	DIR *dp;
	struct dirent *entry;
	char label[256];

	dp = opendir(path);
	if (!dp)
		return;

	while ((entry = readdir(dp))) {
		if (!is_launchd_plist(entry->d_name))
			continue;

		strlcpy(label, entry->d_name, sizeof(label));
		label[strlen(label) - 6] = '\0';
		if (service_name_seen(*seen, *seen_count, label, username))
			continue;

		service_running_status(label, username);
		service_name_add(seen, seen_count, seen_cap, label, username);
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
	char **seen = NULL;
	size_t seen_count = 0, seen_cap = 0;

	if (!ac->system_services && !ac->system_services_process)
		return;

	scan_launchd_dir("/Library/LaunchDaemons", "system", &seen, &seen_count, &seen_cap);
	scan_launchd_dir("/Library/LaunchAgents", "system", &seen, &seen_count, &seen_cap);

	emit_not_found_service_match(ac->services_match ? ac->services_match->hash : NULL,
		&seen, &seen_count, &seen_cap);
	emit_not_found_service_match(ac->services_process_match ? ac->services_process_match->hash : NULL,
		&seen, &seen_count, &seen_cap);

	while (seen_count > 0)
		free(seen[--seen_count]);
	free(seen);
}

void service_user_push(alligator_ht *service_users, char *user)
{
	uint32_t hash;
	system_string_node *node;

	if (!service_users || !user)
		return;

	hash = tommy_strhash_u32(0, user);
	node = alligator_ht_search(service_users, system_string_compare, user, hash);
	if (node)
		return;

	node = calloc(1, sizeof(*node));
	if (!node)
		return;
	node->name = strdup(user);
	if (!node->name) {
		free(node);
		return;
	}

	alligator_ht_insert(service_users, &(node->node), node, hash);
}

void service_user_del(alligator_ht *service_users, char *user)
{
	uint32_t hash;
	system_string_node *node;

	if (!service_users || !user)
		return;

	hash = tommy_strhash_u32(0, user);
	node = alligator_ht_search(service_users, system_string_compare, user, hash);
	if (!node)
		return;

	alligator_ht_remove_existing(service_users, &(node->node));
	free(node->name);
	free(node);
}

static void service_user_free_foreach(void *arg)
{
	system_string_node *node = arg;
	free(node->name);
	free(node);
}

void service_user_clear(alligator_ht *service_users)
{
	if (!service_users)
		return;
	alligator_ht_foreach(service_users, service_user_free_foreach);
	alligator_ht_done(service_users);
}
#endif
