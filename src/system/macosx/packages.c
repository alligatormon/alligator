#ifdef __APPLE__
#include "main.h"
#include "common/deb.h"
#include "common/selector.h"
#include "common/logs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <errno.h>
#include <inttypes.h>

extern aconf *ac;
extern char **environ;

static const char *const brew_candidates[] = {
	"/opt/homebrew/bin/brew",
	"/usr/local/bin/brew",
	NULL
};

static const char *const cellar_roots[] = {
	"/opt/homebrew/Cellar",
	"/usr/local/Cellar",
	NULL
};

/* Reject path-injection tokens before any path join. */
static int brew_token_safe(const char *s)
{
	size_t i;

	if (!s || !s[0])
		return 0;
	if (strstr(s, ".."))
		return 0;
	for (i = 0; s[i]; i++) {
		if (s[i] == '/' || s[i] == '\\')
			return 0;
	}
	return 1;
}

static int64_t cellar_install_ts(const char *cellar, const char *name, const char *version)
{
	char path[512];
	struct stat st;

	if (!brew_token_safe(name) || !brew_token_safe(version))
		return 1;
	if (snprintf(path, sizeof(path), "%s/%s/%s", cellar, name, version) >= (int)sizeof(path))
		return 1;
	if (stat(path, &st) == 0)
		return (int64_t)st.st_mtime;
	return 1;
}

static void emit_formula(const char *name, const char *version, int64_t install_ts)
{
	char *release = "";

	if (!match_mapper(ac->packages_match, (char *)name, strlen(name), (char *)name))
		return;
	metric_add_labels4("package_installed", &install_ts, DATATYPE_INT, ac->system_carg,
		"name", (char *)name, "version", (char *)version, "release", release, "arch", "");
}

/* Primary path: scan Cellar without executing brew. */
static uint64_t scan_cellar_root(const char *cellar)
{
	DIR *root;
	struct dirent *ent;
	uint64_t pkgs = 0;
	struct stat st;

	if (lstat(cellar, &st) != 0 || !S_ISDIR(st.st_mode))
		return 0;

	root = opendir(cellar);
	if (!root)
		return 0;

	while ((ent = readdir(root)) != NULL) {
		char formula_path[512];
		DIR *vdir;
		struct dirent *vent;
		struct stat fst;

		if (ent->d_name[0] == '.')
			continue;
		if (!brew_token_safe(ent->d_name))
			continue;
		if (snprintf(formula_path, sizeof(formula_path), "%s/%s", cellar, ent->d_name) >= (int)sizeof(formula_path))
			continue;
		if (lstat(formula_path, &fst) != 0 || !S_ISDIR(fst.st_mode))
			continue;

		vdir = opendir(formula_path);
		if (!vdir)
			continue;
		while ((vent = readdir(vdir)) != NULL) {
			char ver_path[512];
			struct stat vst;
			int64_t install_ts;

			if (vent->d_name[0] == '.')
				continue;
			if (!brew_token_safe(vent->d_name))
				continue;
			if (snprintf(ver_path, sizeof(ver_path), "%s/%s", formula_path, vent->d_name) >= (int)sizeof(ver_path))
				continue;
			if (lstat(ver_path, &vst) != 0 || !S_ISDIR(vst.st_mode))
				continue;

			++pkgs;
			install_ts = cellar_install_ts(cellar, ent->d_name, vent->d_name);
			emit_formula(ent->d_name, vent->d_name, install_ts);
		}
		closedir(vdir);
	}
	closedir(root);
	return pkgs;
}

static uint64_t scan_all_cellars(void)
{
	uint64_t pkgs = 0;
	int i;

	for (i = 0; cellar_roots[i]; i++)
		pkgs += scan_cellar_root(cellar_roots[i]);
	return pkgs;
}

/*
 * Refuse to exec a user-writable / non-root brew when alligator runs as root.
 * Always require a regular file (not a symlink to an attacker-controlled target
 * we didn't check — follow with lstat on the path we exec; symlink is OK only
 * if the final regular file passes ownership checks after realpath would be ideal;
 * we lstat the candidate path itself and require S_ISREG so symlink-brew is refused).
 */
static int brew_binary_safe_to_exec(const char *path)
{
	struct stat st;

	if (lstat(path, &st) != 0)
		return 0;
	if (!S_ISREG(st.st_mode))
		return 0;
	if (geteuid() == 0) {
		if (st.st_uid != 0)
			return 0;
		if (st.st_mode & (S_IWGRP | S_IWOTH))
			return 0;
	}
	return 1;
}

static const char *find_safe_brew_bin(void)
{
	int i;

	for (i = 0; brew_candidates[i]; i++) {
		if (access(brew_candidates[i], X_OK) != 0)
			continue;
		if (!brew_binary_safe_to_exec(brew_candidates[i])) {
			carglog(ac->system_carg, L_WARN,
				"refusing brew exec of unsafe binary %s (root/writable check)\n",
				brew_candidates[i]);
			continue;
		}
		return brew_candidates[i];
	}
	return NULL;
}

/* Fixed argv, no shell. Returns allocated FILE* on stdout pipe, or NULL. */
static FILE *brew_list_versions_open(const char *brew, pid_t *child_out)
{
	int pipefd[2];
	posix_spawn_file_actions_t actions;
	pid_t pid;
	char *argv[4];
	int rc;

	*child_out = -1;
	if (pipe(pipefd) != 0)
		return NULL;

	argv[0] = (char *)brew;
	argv[1] = "list";
	argv[2] = "--versions";
	argv[3] = NULL;

	if (posix_spawn_file_actions_init(&actions) != 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return NULL;
	}
	posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
	posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
	posix_spawn_file_actions_addclose(&actions, pipefd[0]);
	posix_spawn_file_actions_addclose(&actions, pipefd[1]);

	rc = posix_spawn(&pid, brew, &actions, NULL, argv, environ);
	posix_spawn_file_actions_destroy(&actions);
	close(pipefd[1]);
	if (rc != 0) {
		close(pipefd[0]);
		errno = rc;
		return NULL;
	}

	{
		FILE *fp = fdopen(pipefd[0], "r");
		int status;

		if (fp)
			*child_out = pid;
		else {
			close(pipefd[0]);
			if (waitpid(pid, &status, WNOHANG) == 0) {
				kill(pid, SIGTERM);
				waitpid(pid, &status, 0);
			}
			*child_out = -1;
		}
		return fp;
	}
}

static uint64_t brew_list_via_exec(void)
{
	const char *brew;
	FILE *fd;
	pid_t child;
	char line[1024];
	uint64_t pkgs = 0;
	int status;

	brew = find_safe_brew_bin();
	if (!brew)
		return 0;

	fd = brew_list_versions_open(brew, &child);
	if (!fd) {
		carglog(ac->system_carg, L_ERROR, "brew list --versions spawn failed\n");
		return 0;
	}

	while (fgets(line, sizeof(line), fd)) {
		char *name, *version, *end;
		int64_t install_ts;
		char path[512];
		struct stat st;
		int i;

		end = line + strcspn(line, "\r\n");
		*end = '\0';
		if (!line[0])
			continue;

		name = line;
		version = strchr(line, ' ');
		if (!version)
			continue;
		*version++ = '\0';
		version[strcspn(version, " ")] = '\0';
		if (!brew_token_safe(name) || !brew_token_safe(version))
			continue;

		++pkgs;
		install_ts = 1;
		for (i = 0; cellar_roots[i]; i++) {
			if (snprintf(path, sizeof(path), "%s/%s/%s", cellar_roots[i], name, version) >= (int)sizeof(path))
				continue;
			if (stat(path, &st) == 0) {
				install_ts = (int64_t)st.st_mtime;
				break;
			}
		}
		emit_formula(name, version, install_ts);
	}

	fclose(fd);
	if (child > 0)
		waitpid(child, &status, 0);
	return pkgs;
}

void get_packages_info(void)
{
	uint64_t pkgs;
	static int logged_missing;

	packages_register_metric_families(ac->system_carg);

	pkgs = scan_all_cellars();
	if (pkgs == 0) {
		/* Cellar empty/missing: optional brew fallback with hardened exec. */
		pkgs = brew_list_via_exec();
		if (pkgs == 0 && !logged_missing) {
			carglog(ac->system_carg, L_WARN,
				"Homebrew packages not found (Cellar scan empty; brew unavailable or refused)\n");
			logged_missing = 1;
		}
	}

	metric_add_auto("package_total", &pkgs, DATATYPE_UINT, ac->system_carg);
}
#endif
