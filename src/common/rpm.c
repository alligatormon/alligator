#ifdef __linux__
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include "modules/modules.h"
#include "main.h"
#include "common/rpm.h"
extern aconf *ac;

void rpm_library_free(rpm_library *rpmlib)
{
	if (!rpmlib)
		return;

	if (rpmlib->rpmReadConfigFiles)
	{
		free(rpmlib->rpmReadConfigFiles);
		uv_dlclose(rpmlib->rpmReadConfigFiles_lib);
	}

	if (rpmlib->rpmtsCreate)
	{
		free(rpmlib->rpmtsCreate);
		uv_dlclose(rpmlib->rpmtsCreate_lib);
	}

	if (rpmlib->rpmdbNextIterator)
	{
		free(rpmlib->rpmdbNextIterator);
		uv_dlclose(rpmlib->rpmdbNextIterator_lib);
	}

	if (rpmlib->rpmtsInitIterator)
	{
		free(rpmlib->rpmtsInitIterator);
		uv_dlclose(rpmlib->rpmtsInitIterator_lib);
	}

	if (rpmlib->headerGetEntry)
	{
		free(rpmlib->headerGetEntry);
		uv_dlclose(rpmlib->headerGetEntry_lib);
	}

	if (rpmlib->rpmtsFree)
	{
		free(rpmlib->rpmtsFree);
		uv_dlclose(rpmlib->rpmtsFree_lib);
	}

	if (rpmlib->rpmdbFreeIterator)
	{
		free(rpmlib->rpmdbFreeIterator);
		uv_dlclose(rpmlib->rpmdbFreeIterator_lib);
	}

	free(rpmlib);
}

rpm_library* rpm_library_init()
{
	module_t *module_rpm = tommy_hashdyn_search(ac->modules, module_compare, "rpm", tommy_strhash_u32(0, "rpm"));
	if (!module_rpm)
	{
		if (ac->log_level > 0)
			printf("no module with key rpm\n");
		return NULL;
	}
	char *librpm = module_rpm->path;

	if (ac->log_level > 0)
		printf("initialize RPM from library: %s\n", module_rpm->path);

	rpm_library *rpmlib = calloc(1, sizeof(*rpmlib));

	rpmlib->rpmReadConfigFiles = module_load(librpm, "rpmReadConfigFiles", &rpmlib->rpmReadConfigFiles_lib);
	if (!rpmlib->rpmReadConfigFiles)
	{
		rpm_library_free(rpmlib);
		return NULL;
	}

	rpmlib->rpmtsCreate = (void*)module_load(librpm, "rpmtsCreate", &rpmlib->rpmtsCreate_lib);
	if (!rpmlib->rpmtsCreate)
	{
		rpm_library_free(rpmlib);
		return NULL;
	}

	rpmlib->rpmdbNextIterator = (void*)module_load(librpm, "rpmdbNextIterator", &rpmlib->rpmdbNextIterator_lib);
	if (!rpmlib->rpmdbNextIterator)
	{
		rpm_library_free(rpmlib);
		return NULL;
	}

	rpmlib->rpmtsInitIterator = (void*)module_load(librpm, "rpmtsInitIterator", &rpmlib->rpmtsInitIterator_lib);
	if (!rpmlib->rpmtsInitIterator)
	{
		rpm_library_free(rpmlib);
		return NULL;
	}

	rpmlib->headerGetEntry = module_load(librpm, "headerGetEntry", &rpmlib->headerGetEntry_lib);
	if (!rpmlib->headerGetEntry)
	{
		rpm_library_free(rpmlib);
		return NULL;
	}

	rpmlib->rpmtsFree = module_load(librpm, "rpmtsFree", &rpmlib->rpmtsFree_lib);
	if (!rpmlib->rpmtsFree)
	{
		rpm_library_free(rpmlib);
		return NULL;
	}

	rpmlib->rpmdbFreeIterator = module_load(librpm, "rpmdbFreeIterator", &rpmlib->rpmdbFreeIterator_lib);
	if (!rpmlib->rpmdbFreeIterator)
	{
		rpm_library_free(rpmlib);
		return NULL;
	}

	return rpmlib;
}

void get_rpm_info(rpm_library *rpmlib)
{
	extern aconf *ac;

	char *name, *version, *release;
	rpmts db;

	uint64_t pkgs = 0;
	int64_t *ctime;
	unsigned type, count;
	Header h;
	rpmdbMatchIterator mi;

	if (!ac->rpm_readconf)
	{
		rpmlib->rpmReadConfigFiles(NULL, NULL);
		ac->rpm_readconf = 1;
	}

	db = rpmlib->rpmtsCreate();

	int64_t failedval = 0;

	mi = rpmlib->rpmtsInitIterator(db, RPMDBI_PACKAGES, NULL, 0);
	if (!mi)
		failedval = 1;

	metric_add_auto("rpmdb_load_failed", &failedval, DATATYPE_INT, ac->system_carg);

	while ((h = rpmlib->rpmdbNextIterator(mi)) != NULL)
	{
		ctime = 0;
		rpmlib->headerGetEntry(h, RPMTAG_NAME, &type, (void **) &name, &count);
		rpmlib->headerGetEntry(h, RPMTAG_VERSION, &type, (void **) &version, &count);
		rpmlib->headerGetEntry(h, RPMTAG_RELEASE, &type, (void **) &release, &count);
		rpmlib->headerGetEntry(h, RPMTAG_INSTALLTIME, &type, (void **) &ctime, &count);
		//printf("rpm name %s\n", name);
		//printf("rpm version %s\n", version);
		//printf("rpm release %s\n", release);
		int8_t match = 1;
		if (!match_mapper(ac->packages_match, name, strlen(name), name))
			match = 0;

		if (match)
			metric_add_labels3("package_installed", ctime, DATATYPE_INT, ac->system_carg, "name", name, "release", release, "version", version);
		++pkgs;
	}
	metric_add_auto("package_total", &pkgs, DATATYPE_UINT, ac->system_carg);
	rpmlib->rpmdbFreeIterator(mi);
	rpmlib->rpmtsFree(db);
}
#endif
