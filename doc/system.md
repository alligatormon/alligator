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
    nvml;
    dcgm;
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

Metric naming notes:

- Host uptime metric is `system_uptime_seconds`.
- IPMI metric families are exposed in lowercase snake_case (`ipmi_*`) for Prometheus naming consistency.
- `interface_address` omits interfaces whose name starts with `veth` (same 4-character prefix as `if_stat` / `link_status`). This avoids high-churn Docker container veth series. `iface_num` still counts all interfaces returned by the OS.


## disk
Enables monitoring disk metrics, including the filesystem stats and I/O block devices stats.


## network
Enables the monitoring of the network interfaces and sockets statistics.

`if_stat`, `if_speed`, and `link_status` omit interfaces whose name starts with `veth` (Docker/LXC ephemeral pairs). The same prefix skip is applied to `interface_address` from `base`. Other names that happen to start with `veth` (for example `vethernet`) are also omitted.


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


## nvml
Collects NVIDIA GPU metrics in-process via **NVML** (`libnvidia-ml.so`), without DCGM or `dcgm-exporter`. Linux only.

Requires an explicit library path in the top-level `modules` block (same pattern as `rpmlib` / `libvirt`). There is no automatic soname search.

```
modules {
    nvml /usr/lib64/libnvidia-ml.so.1;
}

system {
    nvml;
}
```

JSON / API:

```json
{
  "modules": { "nvml": "/usr/lib64/libnvidia-ml.so.1" },
  "system": { "nvml": {} }
}
```

If `modules.nvml` is missing or the library fails to load/init, the scrape is skipped (logged). There is **no** automatic fallback to `nvidia-smi`; use the [nvidia_smi aggregate parser](parsers/nvidia-smi.md) when you want the CLI path.

Both collectors may run together: prefixes differ (`nvml_*` vs `nvidia_smi_*`).

Scrapes run on a libuv worker thread so NVML calls do not block the event loop.

### Metrics

Prefix `nvml_`. Per-GPU labels: `name`, `uuid`, `serial`, `index`. Unit suffixes: `_bytes`, `_percent`, `_watt`, `_mhz`, `_celsius`, `_joules`.

| Metric | Notes |
|--------|--------|
| `nvml_gpu_count` | Visible GPU count |
| `nvml_driver_version{version=…}` | Value `1` |
| `nvml_device_info{…,pci_bus_id,brand}` | Value `1` |
| `nvml_utilization_{gpu,memory,encoder,decoder}_percent` | |
| `nvml_memory_{free,used,total}_bytes` | Framebuffer |
| `nvml_temperature_{gpu,memory}_celsius` | Memory temp when supported |
| `nvml_power_usage_watt`, `nvml_power_limit{,_min,_max}_watt` | mW → W |
| `nvml_energy_consumption_joules` | Cumulative since driver load |
| `nvml_clocks_{sm,memory,graphics,video}_mhz` | |
| `nvml_fan_speed_percent` | |
| `nvml_pcie_{tx,rx}_bytes` | Gauge of last sample interval (KB/s → B/s) |
| `nvml_pcie_replay_total` | |
| `nvml_ecc_{corrected,uncorrected}_total` | Volatile aggregate |
| `nvml_retired_pages_{sbe,dbe}_total` | |
| `nvml_clocks_throttle_reasons` | Bitmask + boolean gauges for idle / sw_power / hw_thermal / hw_power_brake |
| `nvml_mig_mode`, `nvml_persistence_mode` | `0`/`1` |
| `nvml_pstate{pstate=P0}` | Value `1` |
| `nvml_process_used_memory_bytes{pid,process_name,type}` | `type=compute\|graphics` when supported |

### Mapping from dcgm-exporter fields

| Alligator metric | Typical DCGM field |
|------------------|--------------------|
| `nvml_clocks_sm_mhz` / `nvml_clocks_memory_mhz` | `DCGM_FI_DEV_SM_CLOCK` / `MEM_CLOCK` |
| `nvml_temperature_gpu_celsius` | `DCGM_FI_DEV_GPU_TEMP` |
| `nvml_power_usage_watt` | `DCGM_FI_DEV_POWER_USAGE` |
| `nvml_energy_consumption_joules` | `DCGM_FI_DEV_TOTAL_ENERGY_CONSUMPTION` |
| `nvml_utilization_gpu_percent` | `DCGM_FI_DEV_GPU_UTIL` |
| `nvml_utilization_memory_percent` | `DCGM_FI_DEV_MEM_COPY_UTIL` |
| `nvml_memory_used_bytes` / `nvml_memory_free_bytes` | `DCGM_FI_DEV_FB_USED` / `FB_FREE` |
| `nvml_fan_speed_percent` | `DCGM_FI_DEV_FAN_SPEED` |

### Gaps vs DCGM

- **`DCGM_FI_PROF_*`** (SM occupancy, tensor pipe, DRAM active, etc.) are collected by [`system { dcgm; }`](#dcgm), not NVML.
- **XID errors** are event-based in NVML; not exported in this collector yet.


## dcgm
Collects NVIDIA **profiling** metrics in-process via embedded **DCGM** (`libdcgm.so`), without running `dcgm-exporter`. Complements [nvml](#nvml) (device health/capacity). Linux only; typically needs datacenter GPUs and DCGM packages.

Requires an explicit library path (no soname auto-search):

```
modules {
    nvml /usr/lib64/libnvidia-ml.so.1;
    dcgm /usr/lib64/libdcgm.so.4;
}

system {
    nvml;
    dcgm;
}
```

JSON / API:

```json
{
  "modules": {
    "nvml": "/usr/lib64/libnvidia-ml.so.1",
    "dcgm": "/usr/lib64/libdcgm.so.4"
  },
  "system": { "nvml": {}, "dcgm": {} }
}
```

Alligator starts an **embedded** DCGM host engine in manual mode, watches identity fields always, and tries each PROF field in its own watch group (~1s). Mixed PROF groups fail on V100 (incompatible counters); unsupported fields are skipped per GPU generation.

Do not run alligator-embedded DCGM and `dcgm-exporter` both watching the same GPUs without care — prefer one sampler.

Unsupported individual fields on a given GPU are omitted, same pattern as NVML.

### Metrics

Prefix `dcgm_`. Per-GPU labels: `name`, `uuid`, `serial`, `index`. Profiling ratios are 0.0–1.0 (not percent).

| Metric | DCGM field |
|--------|------------|
| `dcgm_gpu_count` | supported GPU count |
| `dcgm_profiling_available` | `1` if PROF watches active, else `0` |
| `dcgm_device_info{…,pci_bus_id}` | identity (`DEV_NAME` / `UUID` / …) |
| `dcgm_gr_engine_active_ratio` | `DCGM_FI_PROF_GR_ENGINE_ACTIVE` (1001) |
| `dcgm_sm_active_ratio` | `DCGM_FI_PROF_SM_ACTIVE` (1002) |
| `dcgm_sm_occupancy_ratio` | `DCGM_FI_PROF_SM_OCCUPANCY` (1003) |
| `dcgm_tensor_active_ratio` | `DCGM_FI_PROF_PIPE_TENSOR_ACTIVE` (1004) |
| `dcgm_dram_active_ratio` | `DCGM_FI_PROF_DRAM_ACTIVE` (1005) |
| `dcgm_fp64_active_ratio` / `fp32` / `fp16` | 1006–1008 |
| `dcgm_pcie_{tx,rx}_bytes` | 1009–1010 (sample interval bytes) |
| `dcgm_nvlink_{tx,rx}_bytes` | 1011–1012 |
| `dcgm_tensor_{imma,hmma,dfma}_active_ratio` | 1013–1015 |

### Notes

- Needs NVIDIA **Datacenter GPU Manager** libraries on the host.
- **GeForce RTX 20/30/40**: expect `dcgm_device_info` / `dcgm_gpu_count` and `dcgm_profiling_available 0`. Full `dcgm_*_ratio` needs datacenter / Quadro (or newer SKUs NVIDIA documents as DCP-capable).
- Profiling adds GPU overhead; field set is intentionally small.
- Prefer `nvml` for VRAM/power/temp; use `dcgm` for SM/tensor/DRAM activity when available.


## firewall
Enables the retrieval of counters from the firewall.
The extra parameter 'ipset' allows scraping the list of sets. If the 'entries' option is specified, Alligator will scrape the entries within the ipset.


## cpuavg
Creates a loadavg analog in Linux based only on CPU usage.\
It has one option 'period' which specifies the averaging period in minutes.\
For example, the collected metric looks like this:
```
cpu_usage_average_percent 4.326667
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
<img alt="Dashboard" src="images/dashboard-system.jpg"><br>

In addition, the cAdvisor (docker, containerd, podman, LXC, systemd-nspawn) dashboard is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-cadvisor.json)
<img alt="Dashboard" src="images/dashboard-cadvisor.jpg"><br>

Additionally, the firewall dashboard is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-firewall.json)
