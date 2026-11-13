# eBPF collectors

**Language / Язык:** [English](bpf.md) | [Русский](ru/bpf.md)

The `bpf` context attaches kprobes, uprobes, and a set of built-in probes, then publishes the matching events as metrics. Programs stay attached across scrapes. Each scrape only reads the maps.

This does not change `perf_events_total`, `perf_events_scaling_ratio`, or `perf_kernel_samples_total`. Those stay on `perf_event_open`. Socket counts and TCP states stay on the sock_diag collector. Host PSI metric names are unchanged.

## Requirements

On Linux the collectors are built by default for every build type (Debug, Release, RelWithDebInfo, MinSizeRel). The build needs `clang`, `bpftool`, static `libbpf.a` (system/pkg-config), and Conan `elfutils/0.195` for static `libelf` (zlib comes in through that graph). Alligator does not link `/lib64/libbpf.so.1`. Pass `-DENABLE_BPF=OFF` to leave the collectors out.

At runtime the kernel must be 5.8 or newer and `/sys/kernel/btf/vmlinux` must be readable. Custom kprobe and uprobe probes need 5.15. `lock_contention` needs 5.17 and is skipped on older kernels.

The process needs `CAP_BPF` and `CAP_PERFMON`, or `CAP_SYS_ADMIN`. That is separate from `perf_event_paranoid`, described in [system.md](system.md).

If BTF is missing, the kernel is too old, or the load is denied, alligator logs that once, publishes `bpf_up 0`, and keeps every other collector running. `bpf_up` is omitted when no `bpf` context is configured.

## Configuration

A probe is off until it is listed. The context replaces the previous bpf configuration as a whole.

Full example covering the built-in probes and a latency uprobe (repeat `file_access` prefixes in one probe; do not list the same probe twice):

```
bpf {
    probe file_access { prefix /etc; prefix /var/log; prefix /var/lib; }
    probe runqueue_latency;
    probe offcpu;
    probe block_io;

    probe tcp_retransmit { label dport; }
    probe tcp_connect_latency;
    probe tcp_rtt;
    probe tcp_reset { label dport; }
    probe dns_latency {
        binary /lib64/libc.so.6;
        filter=suffix value=.example.com;
    }

    probe syscall_latency { syscall read; syscall write; syscall openat; }
    probe process_lifecycle;
    probe fd_track { comm nginx; comm worker; }
    probe process_io;

    probe exec { filter=prefix value=/usr/local/bin; }
    probe net_activity;
    probe priv_change;
    probe cgroup_memory;

    uprobe /lib64/libcurl.so.4:curl_easy_perform {
        latency;
        arg 0 type=u64 label=easy;
    }

    cgroup_labels;
}
```

Shorter starter with only scheduling, block I/O, and TCP connect/RTT:

```
bpf {
    probe runqueue_latency;
    probe offcpu;
    probe block_io;
    probe tcp_connect_latency;
    probe tcp_rtt;
    cgroup_labels;
}
```

The same object over the JSON API:

```json
{
  "bpf": {
    "cgroup_labels": true,
    "probes": [
      { "kind": "probe", "name": "runqueue_latency" },
      { "kind": "probe", "name": "offcpu" },
      { "kind": "probe", "name": "block_io" },
      { "kind": "probe", "name": "tcp_connect_latency" },
      { "kind": "probe", "name": "tcp_rtt" }
    ]
  }
}
```

Delete the `bpf` key to detach everything.

## Namespace

Series go to the default namespace. Set `namespace` to publish them somewhere else, then cap that namespace if the labels can grow:

```
bpf {
    namespace bpf;
    probe file_access { prefix /etc; prefix /var/log; }
}

namespace {
    name bpf;
    max_series 4096;
}
```

`namespace custom;` and `namespace=custom;` are the same. Omitting it leaves the series in the default namespace. A `namespace` label on a cgroup series is the Kubernetes namespace, not this setting.

## Custom kprobe and uprobe

Up to six arguments. Each argument has a type, a label, and an optional filter. The filter runs before the event is stored. A value that fails the filter is discarded and does not create a series.

Types:

- `u64` — integer argument, printed in decimal
- `user_str` — userspace string, at most 64 bytes
- `kernel_str` — kernel string, at most 64 bytes
- `sockaddr` — IPv4 or IPv6 address, or the port when the label is `dport` or `port`

Filters are `eq`, `prefix`, `suffix`, and `allow`. `allow` repeats on the same argument. Labels named `path`, `filename`, `file`, or `uid` are rejected unless they have a filter.

```
bpf {
    kprobe tcp_v4_connect {
        arg 1 type=sockaddr label=daddr filter=prefix value=10.;
        arg 1 type=sockaddr label=dport;
    }
    uprobe /lib64/libc.so.6:getaddrinfo {
        arg 0 type=user_str label=name filter=suffix value=.example.com;
    }
}

Every configured argument must pass its filter (AND). Prefer one `allow=` list on a single argument when matching several values.
```

`arg 1` of `tcp_v4_connect` is the kernel `sockaddr` (already copied out of userspace by `move_addr_to_kernel`). Alligator reads it with `bpf_probe_read_kernel` on kprobes and `bpf_probe_read_user` on uprobes. The `host`/`daddr` field is the numeric address (for example `10.1.2.3`), not a DNS name — use a string argument on `getaddrinfo` when filtering by hostname.

Hits that pass the filter increment `bpf_probe_events_total`. The labels are `probe` plus each configured argument label. Pid, a full command line, and a raw address are not labels unless you configure that argument. Each generic probe map keeps 4096 label combinations. Named histogram and counter series from every built-in probe share one userspace staging table of 4096 entries per scrape. Past that, new combinations increment `bpf_probe_dropped_total{probe,reason}` and the oldest entry is recycled so later probes are not starved. A string longer than 64 bytes is counted as a truncation on the same metric.

Add `latency;` (JSON `"latency": true`) to measure function runtime with an entry probe plus a return probe (`kretprobe` / `uretprobe`). Filters still run only on entry: a failed filter does not store a start timestamp and does not increment the counter. When filters pass, the counter increments on entry and the return path records a Prometheus histogram `bpf_probe_latency_seconds` with the same `probe` and argument labels as the counter. Start timestamps are keyed by `(tid, probe_id)` so nested latency probes on one thread do not collide. Without `latency;` behavior is unchanged (counter only, entry attach only).

```
bpf {
    uprobe /lib64/libcurl.so.4:curl_easy_perform {
        latency;
        arg 0 type=u64 label=easy;
    }
    kprobe tcp_v4_connect {
        latency;
        arg 1 type=sockaddr label=daddr filter=prefix value=10.;
    }
}
```

JSON for one kprobe:

```json
{
  "bpf": {
    "probes": [
      {
        "kind": "kprobe",
        "symbol": "tcp_v4_connect",
        "latency": true,
        "args": [
          {
            "index": 1,
            "type": "sockaddr",
            "label": "daddr",
            "filter": "prefix",
            "value": "10."
          }
        ]
      }
    ]
  }
}
```

## Built-in probes

`comm` is the 16-byte task name (15 characters plus a NUL). These probes do not create a series per pid, per argv, or per connection 4-tuple.

`comm name;` inside a probe keeps only that task name. Repeat it for up to 8 names. With no `comm` line the probe records every process. `fd_track` is the exception: it is rejected unless at least one `comm` is set.

```
bpf {
    probe offcpu { comm nginx; comm searchd; }
    probe tcp_retransmit { comm nginx; label dport; }
    kprobe tcp_v4_connect { comm nginx; arg 1 type=sockaddr label=daddr filter=prefix value=10.; }
}
```

### Scheduling and block I/O

```
bpf {
    probe runqueue_latency;
    probe offcpu;
    probe block_io;
    probe lock_contention;
}
```

- `runqueue_latency` — time from wakeup until the task is next on a CPU. Histogram `bpf_runqueue_latency_seconds`. This series has no cgroup label: the wakeup event runs in the waking task.
- `sched_latency` — the same interval and the same histogram. It does not add a second series.
- `offcpu` — time the task is off CPU until it runs again. Histogram `bpf_offcpu_seconds`. With `cgroup_labels`, the cgroup is the task that left the CPU. A task that is exiting is not recorded.
- `block_io` — time from a block request issue to its completion. Histogram `bpf_block_io_latency_seconds` with `dev` (`major:minor`) and `op` (`read`, `write`, or `other`).
- `lock_contention` — time spent waiting on a kernel lock. Histogram `bpf_lock_contention_seconds` by `comm`. There is no lock-name label. On kernels older than 5.17 the probe is skipped and `bpf_probe_skipped{probe="lock_contention"}` is 1.

### TCP and DNS

An address or port is a label only when that probe asks for it.

```
bpf {
    probe tcp_retransmit { label dport; }
    probe tcp_connect_latency;
    probe tcp_rtt;
    probe tcp_reset { label dport; }
    probe dns_latency {
        binary /lib64/libc.so.6;
        filter=suffix value=.example.com;
    }
}
```

- `tcp_retransmit` — counter `bpf_tcp_retransmits_total`. `label dport;` adds the destination port.
- `tcp_connect_latency` — from `SYN_SENT` until the socket is established or the attempt reaches `CLOSE`. Histogram `bpf_tcp_connect_latency_seconds`.
- `tcp_rtt` — smoothed RTT sampled once when a TCP socket becomes established. Histogram `bpf_tcp_rtt_seconds`.
- `tcp_reset` — sent and received TCP resets. Counter `bpf_tcp_resets_total`. `label dport;` adds the destination port when it is available.
- `dns_latency` — `getaddrinfo` in the given binary. The default binary is `/lib64/libc.so.6`. Histogram `bpf_dns_latency_seconds`. The query name is a label only when it matches `filter`. Other lookups stay in the unlabeled series and are not counted twice. The stored name is at most 31 bytes.

### Processes

```
bpf {
    probe syscall_latency { syscall read; syscall write; syscall openat; }
    probe process_lifecycle;
    probe fd_track { comm nginx; comm worker; }
    probe process_io;
}
```

- `syscall_latency` — enter-to-exit time for the listed syscalls. Histogram `bpf_syscall_latency_seconds` with the syscall name. With no `syscall` lines the list is `read`, `write`, `openat`, `futex`, `poll`, and `connect`. Every syscall cannot be attached at once.
- `process_lifecycle` — fork, exec, and exit. Counter `bpf_process_events_total` by `comm` and `event`.
- `fd_track` — open minus close for the listed command names. Gauge `bpf_process_fds` with `comm` and `event="fd"`. The probe is rejected without at least one `comm`.
- `process_io` — bytes from `read` and `write`, by `comm` or cgroup, not by pid. Counter `bpf_process_io_bytes_total` with `event` `read` or `write`.

### Exec, files, network, credentials

These are counters, not an audit log and not a copy of the file or the packet.

```
bpf {
    probe exec { filter=prefix value=/usr/local/bin; }
    probe file_access { prefix /etc; prefix /var/lib; }
    probe net_activity;
    probe priv_change;
}
```

- `exec` — `bpf_exec_total` by `comm` and the parent `comm`. A matching `filter` adds the filename as `path`. Without a filter the counter has no `path` label.
- `file_access` — `openat` path. Counter `bpf_file_access_total`. Nothing is counted until a prefix allowlist is set, either as `prefix /etc;` or `filter=prefix value=/etc;`. `eq`, `suffix`, and `allow` are rejected for this probe.
- `net_activity` — `connect` and `accept`. Counter `bpf_net_activity_total` by `comm` and `event`.
- `priv_change` — credential commit. Counter `bpf_priv_change_total` by `comm` and `changed` (`0` or `1`). The uid is not a label.

### Cgroup memory pressure

```
bpf {
    probe cgroup_memory;
}
```

This reads `memory.pressure` from cgroupfs. It does not attach a BPF program. Host PSI metrics are left as they are.

- `bpf_cgroup_memory_pressure_waiting_seconds_total`
- `bpf_cgroup_memory_pressure_stalled_seconds_total`
- `bpf_cgroup_memory_pressure_waiting_avg_percent{window="10|60|300"}`
- `bpf_cgroup_memory_pressure_stalled_avg_percent{window="10|60|300"}`

Labels are `cgroup` and, when the path is a Kubernetes cgroup, `pod`, `namespace`, and `container`.

## Cgroup labels

`cgroup_labels;` adds `cgroup`, `pod`, `namespace`, and `container` to the scheduling, block, TCP, and process series when the cgroup id can be resolved. The id is the cgroup directory inode. The path is taken from a capped walk of cgroupfs and from cgroups that cadvisor has already seen. There is no kubelet client.

`runqueue_latency` does not get these labels. If the id is known but the path is not, the label is the numeric cgroup id.

## Metrics

Histograms are Prometheus histograms: `_bucket` with `le` in seconds, `_sum` in seconds, and `_count`. Bucket edges are 1µs, 2µs, 4µs, and so on, up to about 1s, then `+Inf`.

| Metric | Type | Labels |
| --- | --- | --- |
| `bpf_up` | gauge | none. `1` while programs are attached, `0` when the context is set but nothing attached |
| `bpf_probe_events_total` | counter | `probe` and the argument labels of a custom kprobe or uprobe |
| `bpf_probe_latency_seconds` | histogram | `probe` and the argument labels of a custom kprobe or uprobe with `latency;` |
| `bpf_probe_dropped_total` | counter | `probe`, `reason` (`lru` or truncation) |
| `bpf_probe_skipped` | gauge | `probe`. `1` when that probe was not attached (kernel too old, program load failed, or attach failed). Custom kprobe/uprobe failures are reported per probe and do not unload other attached custom probes. |
| `bpf_runqueue_latency_seconds` | histogram | `comm` |
| `bpf_offcpu_seconds` | histogram | `comm`, and cgroup labels when enabled |
| `bpf_block_io_latency_seconds` | histogram | `comm`, `dev`, `op` |
| `bpf_lock_contention_seconds` | histogram | `comm` |
| `bpf_tcp_retransmits_total` | counter | `comm`, optional `dport` |
| `bpf_tcp_connect_latency_seconds` | histogram | `comm` |
| `bpf_tcp_rtt_seconds` | histogram | `comm` |
| `bpf_tcp_resets_total` | counter | `comm`, optional `dport` |
| `bpf_dns_latency_seconds` | histogram | `comm`, optional `name` |
| `bpf_syscall_latency_seconds` | histogram | `comm`, syscall name |
| `bpf_process_events_total` | counter | `comm`, `event` |
| `bpf_process_fds` | gauge | `comm`, `event` (`fd`) |
| `bpf_process_io_bytes_total` | counter | `comm`, `event` (`read` or `write`) |
| `bpf_exec_total` | counter | `comm`, `parent`, optional `path` |
| `bpf_file_access_total` | counter | `comm`, `path` |
| `bpf_net_activity_total` | counter | `comm`, `event` |
| `bpf_priv_change_total` | counter | `comm`, `changed` |
| `bpf_cgroup_memory_pressure_*` | counter and gauge | `cgroup`, `pod`, `namespace`, `container`, `window` on the averages |

The collector does not attach USDT probes, does not collect stack traces, and does not load a BPF object supplied in the configuration.
