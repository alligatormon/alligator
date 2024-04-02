#include <stdio.h>
#include <string.h>
#include "main.h"
#include "events/fs_read.h"
extern aconf *ac;

void dpkg_list(char *str, size_t len)
{
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
		strlcpy(pkgname, package, sz+1);

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
			strlcpy(versionname, version, sz+1);
			version += strcspn(version, "-");
			version += strspn(version, "-");
			sz = strcspn(version, "\n");
			strlcpy(releasename, version, sz+1);
		}
		else
		{
			strlcpy(versionname, version, releasesz+1);
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

void dpkg_crawl(char *path)
{
	read_from_file(path, 0, dpkg_callback, NULL);
}
