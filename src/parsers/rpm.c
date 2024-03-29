#ifdef __linux__
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include "modules/modules.h"
#include "main.h"
#include "common/selector.h"
#define RPMEXEC "exec://rpm -qa --queryformat '%{RPMTAG_INSTALLTIME} %{NAME} %{VERSION} %{RELEASE}\n'"
#define RPMLEN 1024

void rpm_handler(char *metrics, size_t size, context_arg *carg)
{
	uint64_t failedval = 0;
	if (!size)
		failedval = 1;

	metric_add_auto("rpmdb_load_failed", &failedval, DATATYPE_UINT, ac->system_carg);

	char field[RPMLEN];
	char version[RPMLEN];
	char name[RPMLEN];
	char release[RPMLEN];
	uint64_t pkgs = 0;

	if (!isdigit(*metrics))
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

void scrape_rpm_info(uv_fs_t *req)
{
	free(req->data);
	uv_fs_req_cleanup(req);

	if (req->result < 0)
	{
		free(req);
		return;
	}
	free(req);

	context_arg *carg = aggregator_oneshot(NULL, RPMEXEC, strlen(RPMEXEC), NULL, 0, rpm_handler, "rpm_handler", NULL, NULL, 0, NULL, NULL, 0, NULL, NULL);
	if (carg)
	{
		carg->no_exit_status = 1;
	}
}

void get_rpm_info()
{
	char *path = malloc(FILENAME_MAX);
	snprintf(path, FILENAME_MAX-1, "%s/bin/rpm", ac->system_usrdir);
	uv_fs_t* req_stat = malloc(sizeof(*req_stat));
	req_stat->data = path;
	uv_fs_stat(uv_default_loop(), req_stat, path, scrape_rpm_info);
}
#endif
