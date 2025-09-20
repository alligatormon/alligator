#ifdef __linux__
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include "events/context_arg.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

int systemd_get_property(sd_bus *bus, char *property, char *expect, char *path) {
	sd_bus_error err = SD_BUS_ERROR_NULL;
	char* msg;
	int ret = 0;

	int r = sd_bus_get_property_string(bus,
		"org.freedesktop.systemd1",
		path,
		"org.freedesktop.systemd1.Unit",
		property,
		&err,
		&msg);

	carglog(ac->system_carg, L_DEBUG, "systemd status returned: '%s', expected: '%s'\n", msg, expect);
	if (r < 0)
	{
		sd_bus_error_free(&err);
		carglog(ac->system_carg, L_DEBUG, "Failed to get %s for service %s. Error: '%s'\n", property, path, err.message);
		return ret;
	}

	sd_bus_error_free(&err);

	if (!strcmp(msg, expect))
		ret = 1;

	free (msg);
	return ret;
}

int systemd_check_service(char *service) {
	sd_bus *bus = NULL;
	int r;

	r = sd_bus_open_system(&bus);
	if (r < 0) {
		carglog(ac->system_carg, L_DEBUG, "Failed to connect to system bus: %s\n", strerror(-r));
		return 0;
	}

	char *path;
	sd_bus_path_encode("/org/freedesktop/systemd1/unit", service, &path);
	carglog(ac->system_carg, L_DEBUG, "systemd converted name from '%s' to path '%s'\n", service, path);

	int ret = systemd_get_property(bus, "ActiveState", "active", path);
	free(path);

	sd_bus_unref(bus);

	return ret;
}

#endif
