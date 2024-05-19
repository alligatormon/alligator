#ifdef __linux__
#include <stdio.h>
#include "main.h"
#include "common/logs.h"
extern aconf *ac;

int systemd_check_service(char *service) {
	char pathsystemd[1000];
	struct stat path_stat;

	snprintf(pathsystemd, 999, "%s/fs/cgroup/systemd/system.slice/%s", ac->system_sysfs, service);
	carglog(ac->system_carg, L_DEBUG, "check systemd service old format within directory: %s\n", pathsystemd);

	if (stat(pathsystemd, &path_stat)) {
		snprintf(pathsystemd, 999, "%s/fs/cgroup/system.slice/%s", ac->system_sysfs, service);
		carglog(ac->system_carg, L_DEBUG, "check systemd service old format the other directory: %s\n", pathsystemd);
	}
	FILE *fd = fopen(pathsystemd, "r");
	if (!fd) {
		carglog(ac->system_carg, L_DEBUG, "check service error: %s\n", pathsystemd);
		return 0;
	}

	carglog(ac->system_carg, L_DEBUG, "check service success: %s\n", pathsystemd);
	fclose(fd);
	return 1;
}
#endif
