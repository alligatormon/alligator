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
	char field[RPMLEN];
	char version[RPMLEN];
	char name[RPMLEN];
	char release[RPMLEN];
	uint64_t pkgs = 0;

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
			metric_add_labels3("package_installed", &ts, DATATYPE_INT, carg, "name", name, "version", version, "release", release);

		++pkgs;
	}

	metric_add_auto("package_total", &pkgs, DATATYPE_UINT, ac->system_carg);
}

void get_rpm_info()
{
	aggregator_oneshot(NULL, RPMEXEC, strlen(RPMEXEC), NULL, 0, rpm_handler, "rpm_handler", NULL, NULL, 0, NULL, NULL, 0);
}
#endif

//string* rpm_mesg(host_aggregator_info *hi, void *arg, void *env, void *proxy_settings)
//{
//	return string_init_alloc("", 0);
//}
//
//void rpm_parser_push()
//{
//	aggregate_context *actx = calloc(1, sizeof(*actx));
//
//	actx->key = strdup("rpm");
//	actx->handlers = 1;
//	actx->handler = calloc(1, sizeof(*actx->handler)*actx->handlers);
//
//	actx->handler[0].name = rpm_handler;
//	actx->handler[0].mesg_func = rpm_mesg;
//	strlcpy(actx->handler[0].key, "rpm", 255);
//
//	alligator_ht_insert(ac->aggregate_ctx, &(actx->node), actx, tommy_strhash_u32(0, actx->key));
//}
