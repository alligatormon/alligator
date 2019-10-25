#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include "modules/modules.h"
#include "main.h"
#include "common/rpm.h"

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

rpm_library* rpm_library_init(const char *librpm)
{
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

	unsigned pkgs = 0;
	int64_t *ctime;
	unsigned type, count;
	Header h;
	rpmdbMatchIterator mi;
	
	rpmlib->rpmReadConfigFiles(NULL, NULL);
	db = rpmlib->rpmtsCreate();
	mi = rpmlib->rpmtsInitIterator(db, RPMDBI_PACKAGES, NULL, 0);
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
		metric_add_labels3("package_installed", ctime, DATATYPE_INT, ac->system_carg, "name", name, "release", release, "version", version);
		++pkgs;
	}
	rpmlib->rpmdbFreeIterator(mi);
	rpmlib->rpmtsFree(db);
}
