# System block

## Overview
Alligator supports system-wide metrics regarding OS, hardware and virtual machines.

There is quick overview of all options in this context:
```
system {
    base;
    disk;
    network;
    ethtool;
    nfs;
    zfs;
    interrupts;
    memory;
    process [nginx] [bash] [/[bash]*/];
    services [nginx.service];
    services_process [php-fpm.service];
    services_checking_users [system] [user] [login1] [login2];
    smart;
    ipmi;
    nvml;
    dcgm;
    amdgpu;
    macos_gpu;
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

When the kernel exposes PSI (`/proc/pressure/*`, Linux 4.20+), Alligator exports:

- `pressure_waiting_seconds_total{resource="cpu|memory|io|irq"}` — cumulative **some** stall time (counter)
- `pressure_stalled_seconds_total{resource="cpu|memory|io|irq"}` — cumulative **full** stall time (counter)
- `pressure_waiting_avg_percent{resource,window}` / `pressure_stalled_avg_percent{resource,window}` — kernel `avg10` / `avg60` / `avg300` stall percent (`window="10|60|300"`)

`irq` is Linux 6.1+ (`/proc/pressure/irq`, typically **full** only). Missing PSI files are skipped silently.

From `/proc/vmstat` (full dump; existing `memory_stat` / `pgscan_total` / `pgsteal_total` stay):

- `vmstat_pages{stat}` — current page counts (`nr_*` and `*_threshold`)
- `vmstat_stat_total{stat}` — cumulative VM events (pgfault, THP, compact, balloon, workingset, …)

From `/proc/sys/fs` (with `open_files_system` / `max_files` from `file-nr`):

- `sysctl_fs{stat}` — `inode_nr`, `inode_free_nr`, `inode_preshrink_nr`, `dentry_nr`, `dentry_unused_nr`, `dentry_age_limit`, `dentry_want_pages`, `aio_nr`, `aio_max_nr`, `dquot_nr`, `dquot_max`, `super_nr`, `super_max`

From `/sys/kernel/mm/ksm` (skipped if KSM is not compiled in; non-numeric files such as `advisor_mode` are ignored):

- `ksm{stat}` — gauges and tunables (`pages_shared`, `pages_sharing`, `pages_unshared`, `pages_volatile`, `run`, `sleep_millisecs`, `pages_to_scan`, `max_page_sharing`, `merge_across_nodes`, `use_zero_pages`, `stable_node_chains`, `stable_node_dups`, `general_profit`, …)
- `ksm_stat_total{stat}` — cumulative counters (`full_scans`, `pages_scanned`)

From `/sys/block/zram*` (skipped if no zram devices; missing per-device files such as `bd_stat` are skipped):

- `zram_bytes{device,stat}` — sizes in bytes: `disksize`, `orig_data_size`, `compr_data_size`, `mem_used_total`, `mem_limit`, `mem_used_max`; backing-device `bd_count` / `bd_reads` / `bd_writes` (kernel 4K units converted to bytes)
- `zram_stat{device,stat}` — gauges: `same_pages`, `huge_pages`, `initstate`
- `zram_stat_total{device,stat}` — counters: `pages_compacted`, `huge_pages_since`, `failed_reads`, `failed_writes`, `invalid_io`, `notify_free`
- `zram_comp_algorithm{device,algorithm}` — value `1` for the active compressor (the name in brackets in `comp_algorithm`)

Block-layer I/O for zram still appears in `disk_io` when `disk` is enabled. Compression ratio:

```promql
zram_bytes{stat="orig_data_size"}
/
zram_bytes{stat="compr_data_size"}
```

CPU time from `/proc/stat` includes modes `user`, `nice`, `system`, `idle`, `iowait`, **`irq`**, **`softirq`**, **`steal`**, and **`guest`** (guest + guest_nice merged) in `cpu_usage_time` / `cpu_usage_core`.

From `/proc/swaps` (per swap device):

- `swap_device_bytes{device,type="size|used|free"}` — bytes
- `swap_device_priority{device}`

From `/proc/schedstat` (per CPU, host scheduler):

- `schedstat_run_time_nanoseconds_total{cpu}`
- `schedstat_runqueue_time_nanoseconds_total{cpu}`
- `schedstat_run_periods_total{cpu}`

Also from `base` (fast scrape):

- `entropy_available_bits`, `entropy_pool_size_bits` — from `/proc/sys/kernel/random/*`
- `selinux_enabled`, `selinux_enforce_mode` — when `/sys/fs/selinux/enforce` exists
- `cpu_frequency_hertz{cpu,type}` — `scaling_cur|scaling_min|scaling_max|cpuinfo_max` from sysfs cpufreq (kHz → Hz)
- `cpu_throttle_count{cpu}`, `cpu_throttle_seconds_total{cpu}` — thermal throttle counters when present
- `cpu_cstate_seconds_total{cpu,state}`, `cpu_cstate_usage_total{cpu,state}`, `cpu_cstate_disabled{cpu,state}` — Linux cpuidle C-states from `/sys/devices/system/cpu/cpuN/cpuidle/stateM/` (`time` µs → seconds, `usage`, `disable`). Missing `cpuidle` dirs (typical in VMs/containers) are skipped. Driver/governor: `cpu_cstate_driver{driver}`, `cpu_cstate_governor{governor}`

C-state residency share (Netdata charts this as a percentage; Alligator exports counters):

```promql
rate(cpu_cstate_seconds_total[5m])
```

Slow scrape (`base`):

- `zoneinfo_stat_total{node,zone,stat}` — `/proc/zoneinfo`
- `numa_meminfo_bytes{node,type}`, `numa_node_stat_total{node,stat}` — per-node sysfs
- `watchdog_stat{device,type}` — `/sys/class/watchdog/*`
- `rapl_energy_joules_total{name,index}` — `/sys/class/powercap` (bare-metal only)

On bare-metal, `get_thermal()` also exports hwmon power and fans (same walk as `core_temperature_celsius`):

- `hwmon_power_watt{name,component,hwmon}` — `power*_input` (microwatts → watts)
- `hwmon_fan_rpm{name,component,hwmon}` — `fan*_input`

`load_average` (`type=load1/5/15`) is the kernel 1/5/15-minute EWMA. On LXC it often reflects the **host**. Overlay spikes with `task_states`, which counts threads from `/proc/<pid>/task/*/stat` in the **current PID namespace**. Do not use `process_states{state="running"}` as a loadavg proxy: that series is thread-group leaders from `/proc` readdir only.

Metric naming notes:

- Host uptime metric is `system_uptime_seconds`.
- IPMI metric families are exposed in lowercase snake_case (`ipmi_*`) for Prometheus naming consistency.
- `interface_address` omits interfaces whose name starts with `veth` (same 4-character prefix as `if_stat` / `link_status`). This avoids high-churn Docker container veth series. `iface_num` still counts all interfaces returned by the OS.


## interrupts
Per-CPU softirq counters from `/proc/softirqs` (normal scrape):

- `softirq_stat_total{cpu,type}`

When `base` is also enabled, `/proc/interrupts` is read on each scrape; with `interrupts` enabled, per-CPU IRQ breakdown is exported as `interrupts_core_count{code,description,cpu}` in addition to `interrupts_by_irq_total{code,description}`.


## memory
Slab caches from `/proc/slabinfo` are collected on the **slow** system scrape interval (~4 minutes), one file read per cycle:

- `slabinfo_objects{slab,type="active|total"}`
- `slabinfo_object_size_bytes{slab}`

Reading `/proc/slabinfo` may require root (file mode `0400` on some kernels).


## disk
Enables monitoring disk metrics, including the filesystem stats and I/O block devices stats.

From `disk` scrape:

- `dmmultipath_stat{device,type}` — multipath DM devices (`size_bytes`, `active`, `paths`, `paths_active`)
- `lvm_lv_size_bytes{vg,lv,device}`, `lvm_lv_suspended{vg,lv,device}` — LVM device-mapper volumes (`/sys/block/dm-*/dm/uuid` starts with `LVM-`)

Slow scrape (`disk`): runtime stats when present — `xfs_stat_total{device,stat}`, `btrfs_stat_total{uuid,stat}`, `bcache_stat_total{uuid,stat}`, `tape_stat_total{device,stat}`.

`disk_usage` / `disk_inodes` include ZFS mounts (`fstype` `zfs`) along with ext*, xfs, btrfs, and tmpfs. ARC and pool health are opt-in `system { zfs; }`.


## network
Enables the monitoring of the network interfaces and sockets statistics.

From `network` scrape:

- `bonding_slaves{master,type="total|active"}`
- `bonding_lacp{master,stat}` — `mode`, `ad_num_ports`, `ad_actor_key`, `ad_partner_key`, `ad_aggregator` from sysfs bonding
- `arp_entries{device}` — ARP table size per interface
- `ipvs_stat_total{stat}` — when `/proc/net/ip_vs_stats` exists
- `network_stat_total{proto,stat}` from `/proc/net/snmp6` (`Ip6`, `Icmp6`, `Udp6`, `UdpLite6`)
- `synproxy_stat_total{stat}` — `/proc/net/stat/synproxy` (hex counters)
- `wireguard_device_listen_port{device}`, `wireguard_device_peers{device}`
- `wireguard_peer_rx_bytes{device,public_key}`, `wireguard_peer_tx_bytes{device,public_key}`, `wireguard_peer_last_handshake_seconds{device,public_key}` — Generic Netlink `WG_CMD_GET_DEVICE`, or a test dump at `$procfs/net/wireguard_dump`

Slow scrape (`network`): `infiniband_stat_total{device,port,stat}`, `fibrechannel_stat_total{host,stat}` when sysfs classes exist.

From `/proc/net/softnet_stat` (when present):

- `softnet_processed_total{cpu}`
- `softnet_dropped_total{cpu}`
- `softnet_times_squeezed_total{cpu}`

From `/proc/net/sockstat` and `/proc/net/sockstat6` (when present):

- `sockstat_sockets_used` — IPv4 only (`sockets: used` is not in sockstat6)
- `sockstat_stat_total{protocol,stat}` — e.g. `protocol="TCP", stat="inuse|orphan|tw|alloc|mem"`; IPv6 uses `TCP6`/`UDP6`/`UDPLITE6`/`RAW6`/`FRAG6` (and `stat="memory"` for FRAG6)

From `/proc/net/wireless` (when present; missing file is skipped):

- `wireless_quality{ifname,type}` — gauges: `status` (hex from the kernel, exported as int), `link`, `level` (dBm), `noise` (dBm; `-256` means the driver did not report noise)
- `wireless_discarded_total{ifname,type}` — counters: `nwid`, `crypt`, `frag`, `retry`, `misc`, `beacon`

This is Wireless Extensions (`iwconfig` / Telegraf `inputs.wireless` / Netdata `proc_net_wireless`). Interface traffic remains in `if_stat`. For SSID / bitrate / per-station RSSI see opt-in [`wifi`](#wifi) (nl80211).

`if_stat`, `if_speed`, and `link_status` omit interfaces whose name starts with `veth` (Docker/LXC ephemeral pairs). The same prefix skip is applied to `interface_address` from `base`. Other names that happen to start with `veth` (for example `vethernet`) are also omitted.


## ethtool
Opt-in NIC driver / IEEE statistics (high cardinality — not enabled by `network`).

```
system {
    ethtool;
}
```

Slow scrape collects (skips `lo` and `veth*`):

- `ethtool_std_stat{ifname,group,stat}` — IEEE / RMON / MAC-control groups via genetlink family `ethtool` (`ETHTOOL_MSG_STATS_GET`, Linux ~5.8+). Groups: `eth_phy`, `eth_mac`, `eth_ctrl`, `rmon`.
- `ethtool_rmon_hist{ifname,direction,bucket_low,bucket_hi}` — RMON size histograms from the same genetlink reply.
- `ethtool_stat{ifname,stat}` — driver-defined counters (`ethtool -S` / node_exporter `node_ethtool_*`) via classic `SIOCETHTOOL` (`ETHTOOL_GSTRINGS` + `ETHTOOL_GSTATS`). Cap 512 keys per iface.

Note: genetlink `STATS_GET` is **not** a reimplementation of ioctl `ETHTOOL_GSTATS`; both paths are collected when `ethtool` is enabled. On NICs like igb/ixgbe expect hundreds–thousands of series (per-queue / VEB / TC counters).


## wifi
Opt-in nl80211 Wi-Fi station/BSS statistics (node_exporter `node_wifi_*`). Not enabled by `network`.

```
system {
    wifi;
}
```

Regular scrape. Live path: `NL80211_CMD_GET_INTERFACE` dump, then per-iface `GET_STATION` and `GET_SCAN` (associated BSS only). Tests / fixtures: `{procfs}/net/nl80211_dump`. Caps: 64 interfaces, 256 stations per iface.

- `wifi_interface_frequency_hertz{ifname}` — operating frequency (kernel MHz × 1e6)
- `wifi_station_info{ifname,bssid,ssid,mode}` — gauge `1`; `mode` is `client` / `ad-hoc` / `unknown`
- `wifi_station_signal_dbm{ifname,mac}`
- `wifi_station_receive_bits_per_second{ifname,mac}` / `wifi_station_transmit_bits_per_second{ifname,mac}`
- `wifi_station_receive_bytes_total{ifname,mac}` / `wifi_station_transmit_bytes_total{ifname,mac}`
- `wifi_station_receive_packets_total{ifname,mac}` / `wifi_station_transmit_packets_total{ifname,mac}`
- `wifi_station_transmit_retries_total{ifname,mac}` / `wifi_station_transmit_failed_total{ifname,mac}`
- `wifi_station_beacon_loss_total{ifname,mac}`
- `wifi_station_connected_seconds_total{ifname,mac}` / `wifi_station_inactive_seconds{ifname,mac}`

WEXT quality/discards stay in `wireless_*` under `network`. AP mode can be high cardinality (one series set per client MAC).


## nfs
Opt-in NFS statistics. Not enabled by `base` or `disk`.

```
system {
    nfs;
}
```

Host-wide RPC totals from `/proc/net/rpc/nfs` and `/proc/net/rpc/nfsd` (same as `nfsstat -c` / `-s`, node_exporter `nfs`/`nfsd`, Netdata NFS client/server):

- `nfs_client_*` / `nfs_server_*` — network, RPC, and per-protocol op counters (`proto_v3_stat`, `proto_v4_stat_op`, reply cache, threads, …)

Per-mount client stats from `/proc/self/mountstats` (node_exporter `mountstats`, Telegraf `nfsclient`). NFSv4 per-operation series are high cardinality, which is why this stays opt-in.

Reads `{procfs}/self/mountstats`. Non-NFS mounts are skipped. Missing files are skipped. Multiple `xprt:` lines (`nconnect`) are summed per protocol (idle = min, max RPC slots = max).

- `nfs_mount_age_seconds{export,mountpoint}` — mount age
- `nfs_mount_info{export,mountpoint,protocol}` — presence (value 1)
- `nfs_mount_bytes_total{export,mountpoint,type}` — `read`, `write`, `direct_read`, `direct_write`, `server_read`, `server_write`, `read_pages`, `write_pages`
- `nfs_mount_event_total{export,mountpoint,type}` — VFS/cache events (`inode_revalidate`, `short_read`, …)
- `nfs_mount_transport_total{export,mountpoint,protocol,type}` — `bind`, `connect`, `sends`, `receives`, `bad_xid`, `backlog`, `sending_queue`, `pending_queue`
- `nfs_mount_transport_idle_seconds{export,mountpoint,protocol}` — seconds since last RPC (TCP)
- `nfs_mount_transport_max_slots{export,mountpoint,protocol}`
- `nfs_mount_ops_total{export,mountpoint,operation,type}` — `ops`, `trans`, `timeouts`, `bytes_sent`, `bytes_recv`, `errors` (errors if the kernel prints a 9th field)
- `nfs_mount_ops_seconds_total{export,mountpoint,operation,type}` — cumulative `queue` / `rtt` / `exe` in **seconds** (kernel values are milliseconds)

Default operations: `READ`, `WRITE`, `GETATTR`, `LOOKUP`, `ACCESS`, `READDIR`, `READDIRPLUS`, `COMMIT`. Average RTT:

```promql
rate(nfs_mount_ops_seconds_total{type="rtt",operation="READ"}[5m])
/
rate(nfs_mount_ops_total{type="ops",operation="READ"}[5m])
```


## zfs
Opt-in OpenZFS statistics from `/proc/spl/kstat/zfs` (Linux SPL). Not enabled by `base` or `disk`. Missing kstat files and a missing kstat directory are skipped.

```
system {
    zfs;
}
```

Reads `{procfs}/spl/kstat/zfs`. No libzfs and no `zpool`/`zfs` CLI.

- `zfs_arc_stat{stat}` — ARC counters (`hits`, `misses`, demand/prefetch hits, `memory_throttle_count`, …)
- `zfs_arc_bytes{stat}` — ARC gauges (`size`, `c`, `c_min`, `c_max`, `mru_size`, `mfu_size`, L2 size, …)
- `zfs_zpool_state{pool,state}` — one-hot pool health (`online`, `degraded`, `faulted`, `offline`, `removed`, `unavail`, `suspended`) from `<pool>/state`
- `zfs_dmu_tx_stat{stat}` — TXG assign/delay/dirty throttle from `dmu_tx`
- `zfs_zil_stat{stat}` — intent log from `zil`

Per-dataset `objset-*` files are **not** collected (high cardinality). Mounted ZFS datasets already appear in `disk_usage` when `disk` is enabled.

ARC hit ratio:

```promql
rate(zfs_arc_stat{stat="hits"}[5m])
/
(rate(zfs_arc_stat{stat="hits"}[5m]) + rate(zfs_arc_stat{stat="misses"}[5m]))
```


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

### Host setup

**1. NVIDIA driver** — same stack as for CUDA/GPU compute. NVML comes from the driver (`libnvidia-ml.so.1`, often via `nvidia-driver-NVML` / driver meta-package). DCGM is **separate** from the driver.

**2. Datacenter GPU Manager (DCGM)** — install the vendor package that ships `libdcgm.so` and the profiling plugin (not buildable from DCGM sources alone):

| Distro | Package (typical) | Library path (check on host) |
|--------|-------------------|------------------------------|
| RHEL / Rocky / Alma | `datacenter-gpu-manager` | `/usr/lib64/libdcgm.so.3` or `.so.4` |
| Ubuntu / Debian | `datacenter-gpu-manager` | `/usr/lib/x86_64-linux-gnu/libdcgm.so.*` |

```bash
# RHEL family
dnf install datacenter-gpu-manager
ls /usr/lib64/libdcgm.so*

# Ubuntu
apt install datacenter-gpu-manager
ls /usr/lib/x86_64-linux-gnu/libdcgm.so*
```

Point `modules.dcgm` at the **real soname** on the host (`readlink -f …` if needed). Alligator does not search `LD_LIBRARY_PATH` for you.

**3. Embedded mode** — alligator calls `dcgmStartEmbedded`; you do **not** need a standalone `nv-hostengine` service or `dcgm-exporter` for these metrics.

**4. GPU SKU**

| GPU class | Expected result |
|-----------|-----------------|
| Tesla / datacenter (V100, A100, …) | `dcgm_profiling_available 1`, SM/DRAM/PCIe ratios |
| GeForce RTX 20/30/40 | identity + `dcgm_profiling_available 0` (no DCP) |
| PCIe cards without NVLink | `dcgm_nvlink_*` absent or zero |

Fields **1013–1015** (IMMA/HMMA/DFMA) are Ampere+; V100 logs `Feature not supported` for them — normal, 12/15 watches is OK.

**5. Privileges** — profiling often needs **root** or `CAP_SYS_ADMIN` (same as `dcgm-exporter` in Docker). Identity fields may work without it; if `dcgm_profiling_available` stays `0` on Tesla, check permissions first.

**6. Conflicts** — pause DCGM profiling while Nsight Compute / Nsight Systems hold the perf counters; stop `dcgm-exporter` / standalone hostengine if they watch the same GPUs.

**7. Verify**

```bash
dcgmi modules -l          # Module 8 "Profiling" → Loaded (not Failed)
curl -s localhost:1111/conf | jq '.modules.dcgm, .system.dcgm'
curl -s localhost:1111 | grep -E 'dcgm_profiling_available|dcgm_sm_active'
# logs: dcgm: profiling watches enabled (N/15 fields)
```

Reload alligator after changing `modules` / `system` config.

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

- Package: **`datacenter-gpu-manager`** (see [Host setup](#host-setup) above).
- Profiling adds GPU overhead; field set is intentionally small.
- Prefer `nvml` for VRAM/power/temp; use `dcgm` for SM/tensor/DRAM activity when available.


## amdgpu
Collects AMD GPU metrics from **amdgpu sysfs** (`/sys/class/drm/cardN/device/`), without ROCm SMI or a vendor library. Linux hosts with the in-tree `amdgpu` driver (consumer Radeon and Instinct). No `modules { }` path is required.

```
system {
    amdgpu;
}
```

JSON / API:

```json
{ "system": { "amdgpu": {} } }
```

Walks `$sysfs/class/drm/card[0-9]+` and keeps devices whose PCI `vendor` is `0x1002` or whose driver is `amdgpu`. `renderD*` and connector nodes are skipped. Missing sysfs files are omitted per GPU (same pattern as NVML). The binary `gpu_metrics` blob is not parsed.

`base` already exports amdgpu hwmon sensors as `core_temperature_celsius` when that collector is enabled; `amdgpu_*` is the dedicated GPU family (utilization, VRAM, clocks, power).

### Metrics

Prefix `amdgpu_`. Per-GPU labels: `name`, `index`, `pci`, `unique_id`. Unit suffixes: `_bytes`, `_percent`, `_watt`, `_mhz`, `_celsius`, `_rpm`.

| Metric | Source |
|--------|--------|
| `amdgpu_gpu_count` | Visible AMD GPU count |
| `amdgpu_device_info{…,vbios}` | `product_name` / PCI id, `vbios_version` |
| `amdgpu_utilization_{gpu,memory}_percent` | `gpu_busy_percent`, `mem_busy_percent` |
| `amdgpu_memory_vram_{total,used,free}_bytes` | `mem_info_vram_*` |
| `amdgpu_memory_gtt_{total,used}_bytes` | `mem_info_gtt_*` |
| `amdgpu_memory_visible_vram_{total,used}_bytes` | `mem_info_vis_vram_*` |
| `amdgpu_clocks_{sclk,mclk}_mhz` | `pp_dpm_sclk` / `pp_dpm_mclk` (current `*`), else hwmon `freq*_input` |
| `amdgpu_temperature_celsius{sensor=edge\|junction\|mem}` | Device hwmon `temp*_input` (millidegrees) |
| `amdgpu_power_{average,cap}_watt` | `power1_average` / `power1_cap` (µW → W) |
| `amdgpu_fan_speed_rpm` | `fan1_input` |


## macos_gpu
Collects Mac GPU metrics via public **IOKit IOAccelerator** properties. Darwin only. Covers Apple Silicon (AGX) and also Intel iGPU / AMD dGPU on Intel Macs when those expose the same keys. No extra framework path; IOKit is already linked.

```
system {
    macos_gpu;
}
```

JSON / API:

```json
{ "system": { "macos_gpu": {} } }
```

Reads `PerformanceStatistics` (utilization, unified memory) plus `model`, `IOClass`, `IONameMatched`, `gpu-core-count`. Private IOReport channels (GPU energy, frequency, thermal) are not used.

### Metrics

Prefix `macos_gpu_`. Per-GPU labels: `name`, `class`, `index`.

| Metric | Notes |
|--------|--------|
| `macos_gpu_gpu_count` | IOAccelerator count |
| `macos_gpu_device_info{…,compat}` | `compat` is `IONameMatched` (e.g. `gpu,t6040`) |
| `macos_gpu_utilization_{device,renderer,tiler}_percent` | From `PerformanceStatistics` |
| `macos_gpu_memory_{alloc,in_use,in_use_driver}_bytes` | Unified/system memory (Apple Silicon UMA) |
| `macos_gpu_memory_device_{alloc,in_use}_bytes` | Dedicated VRAM when present (discrete GPUs) |
| `macos_gpu_core_count` | `gpu-core-count` |
| `macos_gpu_recovery_count` | `recoveryCount` |


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

Alligator exposes:

- `package_installed` — labels `name`, `version`, `release`, `arch`; value is install time (Unix timestamp)
- `package_total` — total number of installed packages seen in the scrape
- `rpmdb_load_failed` — `1` when package data could not be loaded, `0` otherwise (RPM-based hosts only)

`arch` is filled on Debian/Ubuntu, where the same package name can be installed for several
architectures at once; on the other backends it is empty.

Example metric:

```
# HELP package_installed Unix timestamp when the package was installed, labeled by name, version, release and arch.
# TYPE package_installed gauge
package_installed {version="6.40", name="nmap-ncat", release="7.el7", arch=""} 1527797852
package_installed {version="1.31.3-0.2.5-1.0.4", name="nginx", release="0.4.1", arch="amd64"} 1
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

### Debian/Ubuntu data source

On Debian/Ubuntu systems, package metrics are collected from `/var/lib/dpkg/status`; only the stanzas
with `Status: install ok installed` (or `hold ok installed`) are taken into account. `/var/lib/dpkg/available`
is not used: it is a legacy cache of packages *available* in the repositories, it is not maintained by APT
anymore and it says nothing about what is installed. The `rpmlib` module applies only to RPM-based hosts.

dpkg does not store the installation time of a package, so on Debian/Ubuntu the value of
`package_installed` is always `1` and only the label set carries information. The mtime of
`/var/lib/dpkg/info/<package>.list` is sometimes used as a substitute, but it is rewritten on
upgrades and is identical for every package on an image-built host, so it is not exported here.

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
