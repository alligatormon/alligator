#pragma once
 
#include <rpm/rpmlib.h>
#include <rpm/rpmds.h>
#include <rpm/rpmts.h>
#include <rpm/rpmdb.h>

typedef struct rpm_library
{
	void (*rpmReadConfigFiles)(void*, void*);
	uv_lib_t *rpmReadConfigFiles_lib;

	rpmts (*rpmtsCreate)();
	uv_lib_t *rpmtsCreate_lib;

	Header (*rpmdbNextIterator)(rpmdbMatchIterator);
	uv_lib_t *rpmdbNextIterator_lib;

	rpmdbMatchIterator (*rpmtsInitIterator)(rpmts, int, void*, int);
	uv_lib_t *rpmtsInitIterator_lib;

	void (*headerGetEntry)(Header, int, unsigned*, void**, unsigned*);
	uv_lib_t *headerGetEntry_lib;

	void (*rpmtsFree)(rpmts);
	uv_lib_t *rpmtsFree_lib;

	void (*rpmdbFreeIterator)(rpmdbMatchIterator);
	uv_lib_t *rpmdbFreeIterator_lib;

} rpm_library;

rpm_library* rpm_library_init(const char *librpm);
void get_rpm_info(rpm_library *rpmlib);
