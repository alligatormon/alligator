# System block

## Overview
Alligator supports system-wide metrics regarding OS, hardware and virtual machines.

There is quick overview of all options in this context:
```
system {
    base;
    disk;
    network;
    process [nginx] [bash] [/[bash]*/];
    services [nginx.service];
    services_process [php-fpm.service];
    services_checking_users [system] [user] [login1] [login2];
    smart;
    ipmi;
    firewall [ipset=[entries|on]];
    cpuavg period=5;
    packages [nginx] [alligator];
    cadvisor [option1] [option2] .. [optionN];

    pidfile /path/to/pidfile1 [/path/to/pidfile2] ... [/path/to/pidfileN];
    userprocess user1 [user2] ... [userN];
    groupprocess user1 [group2] ... [groupN];
    cgroup /cgroup1/ /[cgroup2]/ ... /[cgroupN]/;

    sysfs  /path/to/dir;
    procfs /path/to/dir;
    rundir /path/to/dir;
    usrdir /path/to/dir;
    etcdir /path/to/dir;
    log_level off;
}
```

## base
Enables monitoring of the base OS and hardware resources, including CPU, memory, and temperatures of the baseboard and components. OS resources such as loadavg, openfiles, interrupts, and context switches are also included.


## disk
Enables monitoring disk metrics, including the filesystem stats and I/O block devices stats.


## network
Enables the monitoring of the network interfaces and sockets statistics.


## process
Specifies a list of processes to check for running and to collect resources usage data in ulimits terms.\
If processes aren't specified, Alligator will collect ulimits usage for all processes.\
This includes metrics like memory usage, CPU usage, disk I/O, open files, threads, and vmaps.


## services
Collects service-level metrics for Linux systemd units.\
Checks whether the service is running and enabled, and reports service task counters.


## services_process
Extended service scraping mode for Linux systemd units.\
Includes all metrics from `services` and additionally scrapes process-level metrics from the service cgroup.

Use this mode only where needed (for selected services), because on heavily forking services it can generate a large amount of process metrics.

Note: historically, `services` also included process scraping. That behavior is now moved to `services_process`.


## services_checking_users
Restricts **which systemd scopes** Alligator walks when collecting `services` / `services_process` metrics (`service_enabled`, `service_running`, `service_tasks_count`, `service_match`).\
When this list is **non-empty**, only the listed scopes are scanned; when it is **empty or unset**, behavior is unchanged (all usual locations, including every active login under `/run/user/*`).

Entries are string tokens:

- **`system`** — system unit directories (`/usr/lib/systemd/system/`, generator trees under `/run/systemd/`, etc.). Metrics for these units use the label `username="system"`.
- **`user`** — shared user-unit trees (`/usr/lib/systemd/user/`, `/etc/systemd/user/`, …). Metrics use the label `username="user"`.
- **Any other token** — treated as a **login name** (`getpwnam`). Alligator scans that user’s units under `/run/user/<uid>/systemd/user/` and `$HOME/.config/systemd/user/`. Metrics use `username="<login>"`.

Plain config lists tokens after the operator, like other system arrays:

```
system {
    services_process [httpd.service];
    services_checking_users [nobody] [system];
}
```

JSON (API or `.json` config) uses an array under `system.services_checking_users`:

```json
"system": {
  "services_process": ["httpd.service"],
  "services_checking_users": ["nobody", "system"]
}
```

On each normal configuration apply, the list is **replaced** in full (not merged incrementally with stale users left over).

**Tip:** combine `services_checking_users` with `services` / `services_process` so only the units you care about are matched inside the reduced scan set.


## smart
Enables the collection of S.M.A.R.T. metrics.


## ipmi
Enables the collection of IPMI metrics using native ioctl() calls. For integration with ipmitool, see this [documentation](https://github.com/alligatormon/alligator/blob/master/doc/parsers/ipmi.md)


## firewall
Enables the retrieval of counters from the firewall.
The extra parameter 'ipset' allows scraping the list of sets. If the 'entries' option is specified, Alligator will scrape the entries within the ipset.


## cpuavg
Creates a loadavg analog in Linux based only on CPU usage.\
It has one option 'period' which specifies the averaging period in minutes.\
For example, the collected metric looks like this:
```
cpu_avg 4.326667
```


## packages
Collects information about packages installed in the OS by the system installer and their installation date.\
It can specify a list of packages. Otherwise, it collects information from all packages in the system.

On RPM-based Linux systems (RHEL, CentOS, Fedora, and similar), Alligator exposes:

- `package_installed` — labels `name`, `version`, `release`; value is install time (Unix timestamp)
- `package_total` — total number of installed packages seen in the scrape
- `rpmdb_load_failed` — `1` when package data could not be loaded, `0` otherwise

Example metric:

```
# HELP package_installed Unix timestamp when the package was installed, labeled by name, version, and release.
# TYPE package_installed gauge
package_installed {version="6.40", name="nmap-ncat", release="7.el7"} 1527797852
# HELP package_total Total number of installed packages seen during the last scrape.
# TYPE package_total gauge
package_total 842
# HELP rpmdb_load_failed 1 if the RPM package database could not be loaded during the last scrape, 0 otherwise.
# TYPE rpmdb_load_failed gauge
rpmdb_load_failed 0
```

Prometheus exposition includes `# HELP` and `# TYPE` lines for each metric family. Without explicit registration, `package_installed` would appear as `# TYPE package_installed unknown` with the metric name repeated as help text.

### RPM data source

On Linux, RPM package metrics are collected in one of two ways:

| Source | When used |
|--------|-----------|
| **rpmlib** | `modules { rpmlib <path>; }` is configured with a path to `librpm` (for example `/usr/lib64/librpm.so.9`) |
| **rpm -qa** | Default when `rpmlib` is not configured, or when loading/using the library fails |

There is no automatic search for `librpm.so.*` sonames. You must set the library path explicitly if you want the `rpmlib` backend.

The `rpmlib` path is read from the top-level `modules` block (key `rpmlib`), the same mechanism used for other dynamic libraries:

```
modules {
    rpmlib /usr/lib64/librpm.so.9;
}

system {
    packages [nginx] [alligator];
}
```

JSON / API configuration:

```json
"modules": {
  "rpmlib": "/usr/lib64/librpm.so.9"
},
"system": {
  "packages": ["nginx", "alligator"]
}
```

When `rpmlib` is configured, Alligator loads the library asynchronously in a libuv worker thread (`uv_dlopen` / `uv_dlsym`), reads the RPM database through the librpm API, and publishes metrics on the event loop. This avoids blocking the main client loop during database access.

If `rpmlib` is missing, cannot be opened, required symbols are absent, or iteration fails, Alligator falls back to:

```
rpm -qa --queryformat '%{RPMTAG_INSTALLTIME} %{NAME} %{VERSION} %{RELEASE}\n'
```

That fallback matches the behavior used on older systems (for example CentOS 7 with Berkeley DB rpmdb) where direct librpm access may be unavailable or undesirable.

### Logging

Datasource selection is logged through the system context logger (`carglog` on `system_carg`) at **info** level when `system.log_level` (or global `log_level`) is `info` or more verbose. Typical messages:

- `rpm packages datasource: rpm -qa (modules.rpmlib is not configured)` — no `rpmlib` module; command backend used
- `rpm packages datasource: rpmlib, loading library <path>` — worker started for configured library
- `rpm packages datasource: rpmlib (library: <path>)` — collection succeeded via librpm
- `rpm packages datasource: rpmlib failed, fallback to rpm -qa (library: <path>, reason: <...>)` — librpm failed; command fallback used

Reason strings include details such as `dlopen` errors, missing symbols, or empty iterator results.

On Debian/Ubuntu systems, package metrics are collected from `/var/lib/dpkg/available` when that file exists; the `rpmlib` module applies only to RPM-based hosts.

## cadvisor
Implements metrics from the well-known exporter called CAdvisor.\

Example of use in the configuration file:
```
system {
    cadvisor [docker=http://unix:/var/run/docker.sock:/containers/json] [log_level=info] [add_labels=collector:cadvisor];
}
```

### log\_level
Specify of the level of logging for this context. Units for this option are explained in this [document](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-log-levels)

### add\_label
This option provides opportunity to manipulate extra labels.

For example, this configuration adds two extra labels for each metric:
```
system {
    cadvisor add_label=label1:value1 add_label=label2:value2;
}
```

### docker
Specifies the socket of the docker daemon. The default is `http://unix:/var/run/docker.sock:/containers/json`.


## pidfile, userprocess, groupprocess, cgroup
Specifies the checking of processes by pidfile, user, group, or cgroup.

Example of use in the configuration file:
```
system {
    pidfile /var/run/nginx.pid;
    userprocess nginx;
    groupprocess nobody;
    cgroup /cpu/;
}
```

## sysfs, procfs, rundir, usrdir, etcdir
Allows the redefinition of the default control directories of the OS, useful for testing and custom-built systems.


## log_level
Default: off\
Plural: no\
Specify of the level of logging for this context. Units for this option are explained in this [document](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-log-levels)

```
system {
    base;
    log_level debug;
}
```


# Dashboard
The system dashboard for Grafana + Prometheus is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-system.json)
<img alt="Dashboard" src="/doc/images/dashboard-system.jpg"><br>

In addition, the cAdvisor (docker, containerd, podman, LXC, systemd-nspawn) dashboard is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-cadvisor.json)
<img alt="Dashboard" src="/doc/images/dashboard-cadvisor.jpg"><br>

Additionally, the firewall dashboard is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-firewall.json)
