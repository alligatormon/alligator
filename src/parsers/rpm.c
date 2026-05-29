#ifdef __linux__
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include "modules/modules.h"
#include "main.h"
#include "common/selector.h"
#include "common/logs.h"
#include "common/deb.h"
#include "events/context_arg.h"
#define RPMEXEC "exec://rpm -qa --queryformat '%{RPMTAG_INSTALLTIME} %{NAME} %{VERSION} %{RELEASE}\n'"
#define RPMLEN 1024
#define RPMDBI_PACKAGES 0
#define RPMTAG_NAME 1000
#define RPMTAG_VERSION 1001
#define RPMTAG_RELEASE 1002
#define RPMTAG_INSTALLTIME 1008

void rpm_handler(char *metrics, size_t size, context_arg *carg)
{
	packages_register_metric_families(ac->system_carg);

	uint64_t failedval = 0;
	if (!metrics || !size)
		failedval = 1;

	metric_add_auto("rpmdb_load_failed", &failedval, DATATYPE_UINT, ac->system_carg);
	if (!metrics || !size)
		return;

	char field[RPMLEN];
	char version[RPMLEN];
	char name[RPMLEN];
	char release[RPMLEN];
	uint64_t pkgs = 0;

	if (!isdigit((unsigned char)*metrics))
	{
		if (ac->log_level > 2)
		{
			printf("rpm handler: format error:\n'%s'\n", metrics);
		}
		return;
	}

	for (uint64_t i = 0; i < size; i++)
	{
		uint64_t j = 0;
		*field = 0;
		str_get_next(metrics, field, RPMLEN, "\n", &i);

		int64_t ts = int_get_next(field, RPMLEN, ' ', &j);
		str_get_next(field, name, RPMLEN, " ", &j);
		++j;
		str_get_next(field, version, RPMLEN, " ", &j);
		++j;
		str_get_next(field, release, RPMLEN, " ", &j);

		int8_t match = 1;
		if (!match_mapper(ac->packages_match, name, strlen(name), name))
			match = 0;
 
		if (match)
			metric_add_labels3("package_installed", &ts, DATATYPE_INT, ac->system_carg, "name", name, "version", version, "release", release);

		++pkgs;
	}

	metric_add_auto("package_total", &pkgs, DATATYPE_UINT, ac->system_carg);
	carg->parser_status = 1;
}

typedef int (*rpmReadConfigFiles_f)(const char*, const char*);
typedef void* (*rpmtsCreate_f)(void);
typedef void* (*rpmtsFree_f)(void*);
typedef void* (*rpmtsInitIterator_f)(void*, int, const void*, size_t);
typedef void* (*rpmdbFreeIterator_f)(void*);
typedef void* (*rpmdbNextIterator_f)(void*);
typedef const char* (*headerGetString_f)(void*, int);
typedef uint64_t (*headerGetNumber_f)(void*, int);

typedef struct rpm_rpmlib_job_s {
	uv_work_t req;
	char *preferred_lib;
	char *metrics;
	size_t metrics_len;
	size_t metrics_cap;
	int failed;
	char err[256];
} rpm_rpmlib_job;

static void spawn_rpm_command_exec(void)
{
	context_arg *carg = aggregator_oneshot(NULL, RPMEXEC, strlen(RPMEXEC), NULL, 0, rpm_handler, "rpm_handler", NULL, NULL, 0, NULL, NULL, 0, NULL, NULL);
	if (carg)
	{
		carg->no_exit_status = 1;
		carg->no_metric = 1;
	}
}

static void spawn_rpm_command(const char *reason)
{
	carglog(ac->system_carg, L_INFO, "rpm packages datasource: rpm -qa (%s)\n", reason ? reason : "unknown");
	spawn_rpm_command_exec();
}

static int rpmlib_append_metric(rpm_rpmlib_job *job, uint64_t ts, const char *name, const char *version, const char *release)
{
	if (!name || !*name || !version || !*version || !release || !*release)
		return 0;

	int line_len = snprintf(NULL, 0, "%llu %s %s %s\n",
				(unsigned long long)ts, name, version, release);
	if (line_len <= 0)
		return -1;

	size_t need = job->metrics_len + (size_t)line_len + 1;
	if (need > job->metrics_cap)
	{
		size_t new_cap = job->metrics_cap ? job->metrics_cap : 4096;
		while (new_cap < need)
			new_cap <<= 1;
		char *new_buf = realloc(job->metrics, new_cap);
		if (!new_buf)
			return -1;
		job->metrics = new_buf;
		job->metrics_cap = new_cap;
	}

	int written = snprintf(job->metrics + job->metrics_len,
			       job->metrics_cap - job->metrics_len,
			       "%llu %s %s %s\n",
			       (unsigned long long)ts, name, version, release);
	if (written <= 0)
		return -1;
	job->metrics_len += (size_t)written;
	job->metrics[job->metrics_len] = 0;
	return 0;
}

static int rpmlib_collect_with_library(const char *lib_name, rpm_rpmlib_job *job)
{
	uv_lib_t lib;
	int rc = uv_dlopen(lib_name, &lib);
	if (rc)
	{
		snprintf(job->err, sizeof(job->err)-1, "dlopen '%s': %s", lib_name, uv_dlerror(&lib));
		return -1;
	}

	rpmReadConfigFiles_f rpmReadConfigFiles = NULL;
	rpmtsCreate_f rpmtsCreate = NULL;
	rpmtsFree_f rpmtsFree = NULL;
	rpmtsInitIterator_f rpmtsInitIterator = NULL;
	rpmdbFreeIterator_f rpmdbFreeIterator = NULL;
	rpmdbNextIterator_f rpmdbNextIterator = NULL;
	headerGetString_f headerGetString = NULL;
	headerGetNumber_f headerGetNumber = NULL;
	void *ts = NULL;
	void *mi = NULL;

	if (uv_dlsym(&lib, "rpmReadConfigFiles", (void**) &rpmReadConfigFiles) ||
	    uv_dlsym(&lib, "rpmtsCreate", (void**) &rpmtsCreate) ||
	    uv_dlsym(&lib, "rpmtsFree", (void**) &rpmtsFree) ||
	    uv_dlsym(&lib, "rpmtsInitIterator", (void**) &rpmtsInitIterator) ||
	    uv_dlsym(&lib, "rpmdbFreeIterator", (void**) &rpmdbFreeIterator) ||
	    uv_dlsym(&lib, "rpmdbNextIterator", (void**) &rpmdbNextIterator) ||
	    uv_dlsym(&lib, "headerGetString", (void**) &headerGetString) ||
	    uv_dlsym(&lib, "headerGetNumber", (void**) &headerGetNumber))
	{
		snprintf(job->err, sizeof(job->err)-1, "dlsym '%s': missing required rpm symbols", lib_name);
		uv_dlclose(&lib);
		return -1;
	}

	rpmReadConfigFiles(NULL, NULL);
	ts = rpmtsCreate();
	if (!ts)
	{
		snprintf(job->err, sizeof(job->err)-1, "rpmtsCreate failed for '%s'", lib_name);
		uv_dlclose(&lib);
		return -1;
	}

	mi = rpmtsInitIterator(ts, RPMDBI_PACKAGES, NULL, 0);
	if (!mi)
	{
		snprintf(job->err, sizeof(job->err)-1, "rpmtsInitIterator failed for '%s'", lib_name);
		rpmtsFree(ts);
		uv_dlclose(&lib);
		return -1;
	}

	for (;;)
	{
		void *h = rpmdbNextIterator(mi);
		if (!h)
			break;

		const char *name = headerGetString(h, RPMTAG_NAME);
		const char *version = headerGetString(h, RPMTAG_VERSION);
		const char *release = headerGetString(h, RPMTAG_RELEASE);
		uint64_t ts64 = headerGetNumber(h, RPMTAG_INSTALLTIME);

		if (rpmlib_append_metric(job, ts64, name, version, release) < 0)
		{
			snprintf(job->err, sizeof(job->err)-1, "buffer append failed for '%s'", lib_name);
			rpmdbFreeIterator(mi);
			rpmtsFree(ts);
			uv_dlclose(&lib);
			return -1;
		}
	}

	rpmdbFreeIterator(mi);
	rpmtsFree(ts);
	uv_dlclose(&lib);

	if (!job->metrics_len)
	{
		snprintf(job->err, sizeof(job->err)-1, "no package rows from '%s'", lib_name);
		return -1;
	}

	return 0;
}

static void rpm_rpmlib_work_cb(uv_work_t *req)
{
	rpm_rpmlib_job *job = req->data;

	if (!job->preferred_lib || !*job->preferred_lib)
	{
		snprintf(job->err, sizeof(job->err) - 1, "rpmlib module path is empty");
		job->failed = 1;
		return;
	}

	if (!rpmlib_collect_with_library(job->preferred_lib, job))
		return;

	job->failed = 1;
}

static const char *get_rpmlib_module_path(void)
{
	module_t *module = alligator_ht_search(ac->modules, module_compare, "rpmlib", tommy_strhash_u32(0, "rpmlib"));
	if (module && module->path && *module->path)
		return module->path;
	return NULL;
}

static void rpm_rpmlib_after_work_cb(uv_work_t *req, int status)
{
	(void)status;
	rpm_rpmlib_job *job = req->data;

	if (!job->failed && job->metrics && job->metrics_len)
	{
		carglog(ac->system_carg, L_INFO, "rpm packages datasource: rpmlib (library: %s)\n",
			job->preferred_lib ? job->preferred_lib : "unknown");
		rpm_handler(job->metrics, job->metrics_len, ac->system_carg);
	}
	else
	{
		carglog(ac->system_carg, L_INFO,
			"rpm packages datasource: rpmlib failed, fallback to rpm -qa (library: %s, reason: %s)\n",
			job->preferred_lib ? job->preferred_lib : "unknown",
			job->err[0] ? job->err : "unknown");
		spawn_rpm_command_exec();
	}

	free(job->preferred_lib);
	free(job->metrics);
	free(job);
}

static void schedule_rpm_rpmlib(const char *lib_path)
{
	carglog(ac->system_carg, L_INFO, "rpm packages datasource: rpmlib, loading library %s\n", lib_path);

	rpm_rpmlib_job *job = calloc(1, sizeof(*job));
	if (!job)
	{
		spawn_rpm_command("failed to allocate rpmlib worker job");
		return;
	}

	job->preferred_lib = strdup(lib_path);
	if (!job->preferred_lib)
	{
		free(job);
		spawn_rpm_command("failed to duplicate rpmlib module path");
		return;
	}

	job->req.data = job;
	if (uv_queue_work(uv_default_loop(), &job->req, rpm_rpmlib_work_cb, rpm_rpmlib_after_work_cb))
	{
		free(job->preferred_lib);
		free(job);
		spawn_rpm_command("uv_queue_work failed for rpmlib worker");
	}
}

void get_rpm_info()
{
	const char *lib_path = get_rpmlib_module_path();
	if (!lib_path)
	{
		spawn_rpm_command("modules.rpmlib is not configured");
		return;
	}

	schedule_rpm_rpmlib(lib_path);
}
#endif
