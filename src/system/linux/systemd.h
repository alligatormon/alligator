#pragma once
#ifdef __linux__
#include <stdint.h>

typedef struct systemd_service_status {
	int running;
	int checked;
	/* Empty when ActiveState is unknown (cgroup-only builds). */
	char active_state[32];
	int has_restarts;
	uint32_t nrestarts;
} systemd_service_status;

/* Fills st. Returns 1 when the unit is ActiveState=active (or cgroup present). */
int systemd_check_service(char *service, char *username, systemd_service_status *st);
#endif
