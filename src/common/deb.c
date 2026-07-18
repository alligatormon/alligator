#include <stdio.h>
#include <string.h>
#include "main.h"
#include "events/fs_read.h"
#include "metric/metric_types.h"
#include "metric/namespace.h"
extern aconf *ac;

void packages_register_metric_families(context_arg *carg)
{
	namespace_metric_family_set(NULL, carg, "package_installed", METRIC_TYPE_GAUGE,
		"Unix timestamp when the package was installed, labeled by name, version, and release.");
	namespace_metric_family_set(NULL, carg, "package_total", METRIC_TYPE_GAUGE,
		"Total number of installed packages seen during the last scrape.");
	namespace_metric_family_set(NULL, carg, "rpmdb_load_failed", METRIC_TYPE_GAUGE,
		"1 if the RPM package database could not be loaded during the last scrape, 0 otherwise.");
}

void dpkg_list(char *str, size_t len)
{
	packages_register_metric_families(ac->system_carg);
	uint64_t i;
	char *package;
	char *version;
	char *newptr;

	char pkgname[255];
	char versionname[255];
	char releasename[255];
	size_t sz;
	size_t releasesz;
	uint64_t pkgs = 0;
	uint64_t datetime = 1;

	for (i=0; i<len; i++)
	{
		++pkgs;
		package = strstr(str+i, "Package:");
		if (!package)
			break;

		package += strcspn(package, " :");
		package += strspn(package, " :");
		sz = strcspn(package, "\n");
		size_t pkg_copy = sz < (sizeof(pkgname) - 1) ? sz : (sizeof(pkgname) - 1);
		strlcpy(pkgname, package, pkg_copy + 1);

		int8_t match = 1;
		if (!match_mapper(ac->packages_match, pkgname, sz, pkgname))
			match = 0;
	
		version = strstr(str+i, "Version:");
		if (!version)
			break;

		version += strcspn(version, " :");
		version += strspn(version, " :");
		sz = strcspn(version, "-");
		releasesz = strcspn(version, "\n");

		if (releasesz > sz)
		{
			size_t ver_copy = sz < (sizeof(versionname) - 1) ? sz : (sizeof(versionname) - 1);
			strlcpy(versionname, version, ver_copy + 1);
			version += strcspn(version, "-");
			version += strspn(version, "-");
			sz = strcspn(version, "\n");
			size_t rel_copy = sz < (sizeof(releasename) - 1) ? sz : (sizeof(releasename) - 1);
			strlcpy(releasename, version, rel_copy + 1);
		}
		else
		{
			size_t ver_copy = releasesz < (sizeof(versionname) - 1) ? releasesz : (sizeof(versionname) - 1);
			strlcpy(versionname, version, ver_copy + 1);
			*releasename = 0;
		}

		if (ac->log_level > 2)
			printf("package: %s, version: %s, releasename: %s\n", pkgname, versionname, releasename);

		if (match)
			metric_add_labels3("package_installed", &datetime, DATATYPE_UINT, ac->system_carg, "name", pkgname, "release", releasename, "version", versionname);

		newptr = strstr(str+i, "\n\n");
		if (!newptr)
			break;

		i += (newptr-(str+i));
	}
	metric_add_auto("package_total", &pkgs, DATATYPE_UINT, ac->system_carg);
}

//int main()
//{
//	//FILE *fd = fopen("/var/lib/dpkg/available", "r");
//	FILE *fd = fopen("test.list", "r");
//	if (!fd)
//		return 1;
//
//	char buf[64*1024*1024];
//	size_t rc = fread(buf, 1, 64*1024*1024, fd);
//
//	dpkg_list(buf, rc);
//}

void dpkg_callback(char *buf, size_t len, void *data)
{
	if (buf && len > 1)
		dpkg_list(buf, len);
}

void dpkg_stat_cb(uv_fs_t* req)
{
	if (req->result < 0) {
		return;
	}

	read_from_file(strdup(req->data), 0, dpkg_callback, NULL);
	free(req);
}

void dpkg_crawl(char *path)
{
	uv_fs_t *req = malloc(sizeof(uv_fs_t));
	req->data = path;
	uv_fs_stat(uv_default_loop(), req, path, dpkg_stat_cb);
}
