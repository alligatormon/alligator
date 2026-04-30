#ifdef __linux__
#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include "events/context_arg.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

static int systemd_trace_service(const char *service)
{
	return service && strstr(service, "attachi") != NULL;
}

static int cgroup_has_pids(const char *path)
{
	FILE *fd;
	char line[32];

	fd = fopen(path, "r");
	if (!fd)
		return 0;

	if (!fgets(line, sizeof(line), fd))
	{
		fclose(fd);
		return 0;
	}
	fclose(fd);
	return line[0] != '\0' && line[0] != '\n';
}

static int systemd_check_user_service_via_cgroup(const char *uid, const char *service)
{
	char path[PATH_MAX];
	int ret = 0;

	snprintf(path, sizeof(path), "%s/fs/cgroup/user.slice/user-%s.slice/user@%s.service/app.slice/%s/cgroup.procs", ac->system_sysfs, uid, uid, service);
	ret = cgroup_has_pids(path);
	if (ret)
		return 1;

	snprintf(path, sizeof(path), "%s/fs/cgroup/user.slice/user-%s.slice/user@%s.service/session.slice/%s/cgroup.procs", ac->system_sysfs, uid, uid, service);
	ret = cgroup_has_pids(path);
	if (ret)
		return 1;

	snprintf(path, sizeof(path), "%s/fs/cgroup/user.slice/user-%s.slice/user@%s.service/%s/cgroup.procs", ac->system_sysfs, uid, uid, service);
	ret = cgroup_has_pids(path);
	if (ret)
		return 1;

	return 0;
}

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
	if (r < 0)
	{
		carglog(ac->system_carg, L_DEBUG, "Failed to get %s for service %s. Error: '%s'\n", property, path, err.message ? err.message : "unknown");
		sd_bus_error_free(&err);
		return ret;
	}

	carglog(ac->system_carg, L_DEBUG, "systemd status returned: '%s', expected: '%s'\n", msg, expect);

	sd_bus_error_free(&err);

	if (!strcmp(msg, expect))
		ret = 1;

	free (msg);
	return ret;
}

static int systemd_get_unit_path(sd_bus *bus, const char *service, char **unit_path)
{
	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	int r;

	*unit_path = NULL;
	r = sd_bus_call_method(bus, "org.freedesktop.systemd1", "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "GetUnit", &err, &reply, "s", service);
	if (r < 0)
	{
		carglog(ac->system_carg, L_DEBUG, "GetUnit failed for '%s': %s\n", service, err.message ? err.message : "unknown");
		if (systemd_trace_service(service))
			carglog(ac->system_carg, L_INFO, "systemd-trace GetUnit failed for '%s': %s (%d)\n", service, err.message ? err.message : "unknown", r);
		sd_bus_error_free(&err);
		return r;
	}

	r = sd_bus_message_read(reply, "o", unit_path);
	if (r < 0)
	{
		carglog(ac->system_carg, L_DEBUG, "Failed to read GetUnit reply for '%s': %s\n", service, strerror(-r));
		if (systemd_trace_service(service))
			carglog(ac->system_carg, L_INFO, "systemd-trace GetUnit reply read failed for '%s': %s (%d)\n", service, strerror(-r), r);
		sd_bus_message_unref(reply);
		sd_bus_error_free(&err);
		return r;
	}

	*unit_path = strdup(*unit_path);
	sd_bus_message_unref(reply);
	sd_bus_error_free(&err);
	if (!*unit_path)
		return -ENOMEM;
	return 0;
}

static int systemd_check_service_on_bus(sd_bus *bus, char *service)
{
	char *path = NULL;
	int ret = 0;
	int r = systemd_get_unit_path(bus, service, &path);
	if (r < 0 || !path)
	{
		r = sd_bus_path_encode("/org/freedesktop/systemd1/unit", service, &path);
		if (r < 0 || !path)
		{
			carglog(ac->system_carg, L_DEBUG, "Failed to encode unit path for '%s': %s\n", service, strerror(-r));
			if (systemd_trace_service(service))
				carglog(ac->system_carg, L_INFO, "systemd-trace path encode failed for '%s': %s (%d)\n", service, strerror(-r), r);
			return 0;
		}
	}

	carglog(ac->system_carg, L_DEBUG, "systemd converted name from '%s' to path '%s'\n", service, path);
	if (systemd_trace_service(service))
		carglog(ac->system_carg, L_INFO, "systemd-trace checking '%s' at path '%s'\n", service, path);
	ret = systemd_get_property(bus, "ActiveState", "active", path);
	if (systemd_trace_service(service))
		carglog(ac->system_carg, L_INFO, "systemd-trace result for '%s': %d\n", service, ret);
	free(path);
	return ret;
}

int systemd_check_service(char *service, char *username, int *checked) {
	const char *run_user_root = "/run/user";
	DIR *root_dir = NULL;
	struct dirent *entry;
	sd_bus *bus = NULL;
	int r;
	int ret = 0;
	int checked_local = 0;
	char service_buf[PATH_MAX];
	const char *resolved_service = service;
	uid_t target_uid = (uid_t) -1;
	int target_user_only = 0;

	if (checked)
		*checked = 0;

	if (service && !strchr(service, '.'))
	{
		snprintf(service_buf, sizeof(service_buf), "%s.service", service);
		resolved_service = service_buf;
	}

	if (username && strcmp(username, "system") && strcmp(username, "user"))
	{
		struct passwd *pwd = getpwnam(username);
		if (pwd)
		{
			target_uid = pwd->pw_uid;
			target_user_only = 1;
		}
	}

	if (!target_user_only)
	{
		r = sd_bus_open_system(&bus);
		if (r < 0) {
			carglog(ac->system_carg, L_DEBUG, "Failed to connect to system bus: %s\n", strerror(-r));
			if (systemd_trace_service(resolved_service))
				carglog(ac->system_carg, L_INFO, "systemd-trace system bus open failed for '%s': %s (%d)\n", resolved_service, strerror(-r), r);
		} else {
			checked_local = 1;
			if (systemd_trace_service(resolved_service))
				carglog(ac->system_carg, L_INFO, "systemd-trace checking system bus for '%s'\n", resolved_service);
			ret = systemd_check_service_on_bus(bus, (char*) resolved_service);
			sd_bus_unref(bus);
			bus = NULL;
			if (ret)
			{
				if (checked)
					*checked = 1;
				return 1;
			}
		}
	}

	root_dir = opendir(run_user_root);
	if (!root_dir)
	{
		if (checked)
			*checked = checked_local;
		return 0;
	}

	while ((entry = readdir(root_dir)) != NULL)
	{
		char sock_path[PATH_MAX];
		char address[PATH_MAX + 16];
		sd_bus *user_bus = NULL;
		int i;

		if (entry->d_name[0] == '.')
			continue;

		for (i = 0; entry->d_name[i]; ++i)
		{
			if (entry->d_name[i] < '0' || entry->d_name[i] > '9')
				break;
		}

		if (entry->d_name[i] != '\0')
			continue;

		if (target_user_only && (uid_t) strtoul(entry->d_name, NULL, 10) != target_uid)
			continue;

		snprintf(sock_path, sizeof(sock_path), "%s/%s/bus", run_user_root, entry->d_name);
		snprintf(address, sizeof(address), "unix:path=%s", sock_path);

		r = sd_bus_new(&user_bus);
		if (r < 0 || !user_bus)
			continue;

		r = sd_bus_set_address(user_bus, address);
		if (r < 0)
		{
			sd_bus_unref(user_bus);
			continue;
		}

		r = sd_bus_set_bus_client(user_bus, 1);
		if (r < 0)
		{
			if (systemd_trace_service(resolved_service))
				carglog(ac->system_carg, L_INFO, "systemd-trace set bus client failed '%s': %s (%d)\n", address, strerror(-r), r);
			sd_bus_unref(user_bus);
			continue;
		}

		r = sd_bus_start(user_bus);
		if (r < 0)
		{
			carglog(ac->system_carg, L_DEBUG, "Failed to connect user bus '%s': %s\n", address, strerror(-r));
			if (systemd_trace_service(resolved_service))
				carglog(ac->system_carg, L_INFO, "systemd-trace user bus connect failed '%s': %s (%d)\n", address, strerror(-r), r);
			ret = systemd_check_user_service_via_cgroup(entry->d_name, resolved_service);
			checked_local = 1;
			if (systemd_trace_service(resolved_service))
				carglog(ac->system_carg, L_INFO, "systemd-trace cgroup fallback uid=%s service='%s' ret=%d (after bus connect fail)\n", entry->d_name, resolved_service, ret);
			sd_bus_unref(user_bus);
			if (ret)
				break;
			continue;
		}

		if (systemd_trace_service(resolved_service))
			carglog(ac->system_carg, L_INFO, "systemd-trace connected user bus '%s' for '%s'\n", address, resolved_service);
		ret = systemd_check_service_on_bus(user_bus, (char*) resolved_service);
		checked_local = 1;
		sd_bus_unref(user_bus);
		if (ret)
			break;

		ret = systemd_check_user_service_via_cgroup(entry->d_name, resolved_service);
		checked_local = 1;
		if (systemd_trace_service(resolved_service))
			carglog(ac->system_carg, L_INFO, "systemd-trace cgroup fallback uid=%s service='%s' ret=%d\n", entry->d_name, resolved_service, ret);
		if (ret)
			break;
	}

	closedir(root_dir);
	if (checked)
		*checked = checked_local;
	return ret;
}

#endif
