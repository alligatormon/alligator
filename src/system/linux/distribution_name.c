#include <stdio.h>
#include "main.h"
extern aconf *ac;

void get_distribution_name()
{
	char fname[255];
	char buf[255];
	char *ptr;
	char name[255];
	char version[255];
	char pretty_name[255];
	snprintf(fname, 254, "%s/os-release", ac->system_etcdir);
	FILE *fd = fopen(fname, "r");
	if (!fd)
		return;

	while (fgets(buf, 255, fd))
	{
		if (!strncmp(buf, "NAME", 4))
		{
			ptr = buf + 4 + strcspn(buf+4, "=\"");
			ptr += strspn(ptr, "=\"");
			ptr[strcspn(ptr, "\r\n\"")] = 0;
			strlcpy(name, ptr, 255);
		}
		else if (!strncmp(buf, "VERSION", 7))
		{
			ptr = buf + 7 + strcspn(buf+7, "=\"");
			ptr += strspn(ptr, "=\"");
			ptr[strcspn(ptr, "\r\n\"")] = 0;
			strlcpy(version, ptr, 255);
		}
		else if (!strncmp(buf, "PRETTY_NAME", 11))
		{
			ptr = buf + 11 + strspn(buf+11, "=\"");
			ptr += strspn(ptr, "=\"");
			ptr[strcspn(ptr, "\r\n\"")] = 0;
			strlcpy(pretty_name, ptr, 255);
		}
	}

	uint64_t okval = 1;
	metric_add_labels4("os_distribution", &okval, DATATYPE_UINT, ac->system_carg, "os", "linux", "name", name, "version", version, "pretty", pretty_name);
	fclose(fd);
}
