#ifdef __FreeBSD__
#include "main.h"
#include "common/deb.h"
#include "common/selector.h"
#include "common/logs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

extern aconf *ac;

void get_packages_info(void)
{
	FILE *fd;
	char line[1024];
	uint64_t pkgs = 0;
	const char *release = "";

	packages_register_metric_families(ac->system_carg);

	fd = popen("pkg query -a '%k %n %v'", "r");
	if (!fd) {
		carglog(ac->system_carg, L_ERROR, "pkg query failed");
		return;
	}

	while (fgets(line, sizeof(line), fd)) {
		char *name, *version, *end;
		uint64_t ts;
		int64_t install_ts;

		name = strchr(line, ' ');
		if (!name)
			continue;
		*name++ = '\0';

		version = strchr(name, ' ');
		if (!version)
			continue;
		*version++ = '\0';

		end = version + strcspn(version, "\r\n");
		*end = '\0';

		ts = strtoull(line, NULL, 10);
		if (!ts)
			ts = 1;
		install_ts = (int64_t)ts;
		++pkgs;

		if (!match_mapper(ac->packages_match, name, strlen(name), name))
			continue;

		metric_add_labels3("package_installed", &install_ts, DATATYPE_INT, ac->system_carg,
			"name", name, "version", version, "release", release);
	}

	pclose(fd);
	metric_add_auto("package_total", &pkgs, DATATYPE_UINT, ac->system_carg);
}
#endif
