#ifdef __linux__
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include "events/context_arg.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

int systemd_get_property(sd_bus *bus, char *property, char *expect, char *m_unit) {
	sd_bus_error err = SD_BUS_ERROR_NULL;
	char* msg;
	int ret = 0;

	char path[255];
	*path = 0;
	snprintf(path, 254, "/org/freedesktop/systemd1/unit/%s", m_unit);
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
		carglog(ac->system_carg, L_DEBUG, "Failed to get %s for service %s. Error: '%s'\n", property, m_unit, err.message);
		return ret;
	}

	sd_bus_error_free(&err);

	if (!strcmp(msg, expect))
		ret = 1;

	free (msg);
	return ret;
}

uint16_t systemd_convert_name(char *dst, char *src) {
	uint16_t j = 0;
	for (uint16_t i = 0; src[i]; ++i) {
		if (src[i] == '.') {
			dst[j++] = '_';
			dst[j++] = '2';
			dst[j++] = 'e';
		}
		else if (src[i] == '-') {
			dst[j++] = '_';
			dst[j++] = '2';
			dst[j++] = 'd';
		}
		else {
			dst[j++] = src[i];
		}
	}

	dst[j] = 0;

	carglog(ac->system_carg, L_DEBUG, "systemd convert name from '%s' to '%s'\n", src, dst);
	return j;
}

int systemd_check_service(char *service) {
	sd_bus *bus = NULL;
	int r;

	r = sd_bus_open_system(&bus);
	if (r < 0) {
		carglog(ac->system_carg, L_DEBUG, "Failed to connect to system bus: %s\n", strerror(-r));
		return 0;
	}

	char buf[255];
	systemd_convert_name(buf, service);
	int ret = systemd_get_property(bus, "ActiveState", "active", buf);

	sd_bus_unref(bus);

	return ret;
}

#endif
