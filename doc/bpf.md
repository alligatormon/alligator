# eBPF collectors

**Language / Язык:** [English](bpf.md) | [Русский](ru/bpf.md)

The `bpf` context attaches kprobes, uprobes, and a set of built-in probes, then publishes the matching events as metrics. Programs stay attached across scrapes. Count and latency maps are read on each scrape. Capture events are drained from a ring buffer as they arrive.

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

Add `latency;` (JSON `"latency": true`) to measure function runtime with an entry probe plus a return probe (`kretprobe` / `uretprobe`). Filters still run only on entry: a failed filter does not store a start timestamp and does not increment the counter. When filters pass, the counter increments on entry and the return path records a Prometheus histogram `bpf_probe_latency_seconds` with the same `probe` and argument labels as the counter. Start timestamps are keyed by `(tid, probe_id)` so nested latency probes on one thread do not collide. Without `latency;` behavior is unchanged (counter only, entry attach only). Omit `mode`, or set `mode count;`, for this path. `mode capture;` is separate and does not record latency.

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

## Capture mode

`mode capture;` attaches dedicated capture programs (`generic_kprobe_capture`, `generic_uprobe_capture`, `generic_uretprobe_capture`), not the count and latency programs. Each call copies one buffer into a 1MiB ring buffer. Userspace runs a PCRE1 pattern on that buffer. Named groups become Prometheus labels. The pattern is not compiled into eBPF, and Alligator does not reassemble a byte stream: each `read`/`write` is one event. A partial message that does not match is dropped.

This is not an HTTP or TLS parser. OpenSSL below is only an example of entry arguments versus a return-time buffer. `curl -v` lines such as `GET / HTTP/2` are logical; with HTTP/2 the bytes in `SSL_write` / `SSL_read` are the client connection preface and binary frames (SETTINGS, HEADERS with HPACK, and so on), not an ASCII request or status line.

Write-side patterns below match either the HTTP/2 client preface (`PRI * HTTP/2.0…`, label `h2`) or an HTTP/1.x request line (`method`, `path`, `version`). Read-side patterns match an HTTP/1.x status line, or the same HTTP/2 preface if a peer ever sends it on the read path (servers normally do not; HTTP/2 response status lives in HPACK HEADERS and is not extracted here). SETTINGS, WINDOW_UPDATE, HEADERS, and body chunks still miss and increment `regex_nomatch`. Named group names in one pattern must be unique: reusing a name (for example `http_version` on both alternatives) fails PCRE compile and every event becomes `regex_fail`.

```
bpf {
    namespace bpf_usdt;
    uprobe /lib64/libssl.so.3:SSL_write {
        mode capture;
        metric bpf_us_fn_events_total;
        comm curl;
        arg 1 type=user_buffer size=arg2 max=512 label=buf;
        regex on=buf pattern="^(?:PRI \* HTTP\/(?P<h2>2)\.0|(?P<method>GET|POST)\s+(?P<path>\S+)\s+HTTP\/(?P<version>\d+(?:\.\d+)?))";
    }
    uprobe /lib64/libssl.so.3:SSL_write_ex {
        mode capture;
        pair_id ssl_write_ex;
        save arg1;
        save arg3;
    }
    uretprobe /lib64/libssl.so.3:SSL_write_ex {
        mode capture;
        pair_id ssl_write_ex;
        metric bpf_us_fn_events_total;
        comm curl;
        when retval_gt 0;
        arg 1 type=user_buffer size=deref:saved_arg3 max=512 label=buf from=saved;
        regex on=buf pattern="^(?:PRI \* HTTP\/(?P<h2>2)\.0|(?P<method>GET|POST)\s+(?P<path>\S+)\s+HTTP\/(?P<version>\d+(?:\.\d+)?))";
    }
    uprobe /lib64/libssl.so.3:SSL_read {
        mode capture;
        pair_id ssl_read;
        save arg1;
    }
    uretprobe /lib64/libssl.so.3:SSL_read {
        mode capture;
        pair_id ssl_read;
        metric bpf_us_fn_events_total;
        comm curl;
        when retval_gt 0;
        arg 1 type=user_buffer size=retval max=512 label=buf from=saved;
        regex on=buf pattern="^(?:PRI \* HTTP/(?P<h2>2)\.0|HTTP/(?P<version>\d+(?:\.\d+)?)\s+(?P<status>\d{3})\b)";
    }
    uprobe /lib64/libssl.so.3:SSL_read_ex {
        mode capture;
        pair_id ssl_read_ex;
        save arg1;
        save arg3;
    }
    uretprobe /lib64/libssl.so.3:SSL_read_ex {
        mode capture;
        pair_id ssl_read_ex;
        metric bpf_us_fn_events_total;
        comm curl;
        when retval_gt 0;
        arg 1 type=user_buffer size=deref:saved_arg3 max=512 label=buf from=saved;
        regex on=buf pattern="^(?:PRI \* HTTP/(?P<h2>2)\.0|HTTP/(?P<version>\d+(?:\.\d+)?)\s+(?P<status>\d{3})\b)";
    }
}

namespace {
    name bpf_usdt;
    max_series 4096;
}
```

In plain config a quoted pattern keeps the backslashes you write. PCRE `\s` is written `\s`. `{` inside the quotes is part of the pattern, not a new block. `/` in `HTTP/` does not need escaping.

`SSL_write(SSL *, buf, num)` can be copied on entry: argument 1 is the buffer and argument 2 is the length. Newer curl/OpenSSL builds often call `SSL_write_ex` instead of `SSL_write`; attach both if you need coverage. `SSL_read` fills the buffer before it returns, so the entry probe only `save`s the pointer and the `uretprobe` reads `min(retval, max)` bytes from that pointer. `SSL_write_ex` / `SSL_read_ex` pass `size_t *written` / `size_t *readbytes`; the return probe dereferences the saved pointer (`deref:saved_arg3`, an 8-byte native integer). `when retval_gt 0` drops a non-positive return before the copy. `pair_id` ties the entry stash to the return probe. The stash key is `(tid, pair_id)`. One thread re-entering the same pair overwrites the saved registers.

`arg` fields for `type=user_buffer`:

- `size=argN` — length is that integer argument
- `size=retval` — length is the return value (`uretprobe` only)
- `size=deref:argN` — length is a `u64` read from the pointer in argument N
- `size=deref:saved_argN` — same, using the pointer saved on entry
- `max=N` — copy at most N bytes, hard-capped at 512. A longer source increments `bpf_probe_dropped_total{reason="trunc"}` and the event is still submitted
- `from=saved` — buffer pointer comes from the entry stash
- `label` — name used by `regex on=...`. The raw buffer is not a label
- `type=user_buffer` is rejected unless the probe is `mode capture`

`regex on=LABEL pattern="..."` runs `pcre_exec` on that buffer's copied length, not on a C string. No match drops the event (`reason="regex_nomatch"`). A pattern that fails to compile (including duplicate named groups without `PCRE_DUPNAMES`) drops every event from that probe (`reason="regex_fail"`); look for `bpf: probe '...' regex failed` in the log. Up to six named groups become labels. The series also has `probe` and `comm`. On a uretprobe, `probe` is `binary:symbol:ret`. Without `metric`, the name is `bpf_us_fn_events_total`.

`kprobe` capture reads a user pointer on entry. Return capture is `uretprobe` only. `latency;` on a capture probe is ignored.

A config with no `mode capture` does not load the capture programs, so a capture verifier failure cannot block count or latency. In a mixed config, if the capture programs fail to load, those probes are skipped (`bpf_probe_skipped`) and the count and latency probes still attach. If the capture ring buffer cannot be opened, the capture links are detached and those probes are skipped. They are not left running without a consumer.

The ring buffer is 1MiB. Userspace drains it continuously: libuv polls the ring-buffer file descriptor and consumes a bounded batch on each wakeup, and the scrape path consumes again as a safety net. If the buffer fills before it is drained, `reason="ringbuf_full"`. A failed `bpf_probe_read_user` is `reason="user_read_fail"`. A return that asked for a saved pointer and did not find the stash is `reason="stash_miss"`. Capture keeps its own table of 4096 label sets. Past that, new label sets are dropped and `reason="lru"` increments. Existing counters are not reset, so a series does not look like it restarted at 1.

If the bpf namespace has no explicit `max_series`, capture sets it to 4096. An explicit value, including `0` (unlimited), is kept. That cap is what stops capture from growing the metric tree without a bound.

Named regex groups become Prometheus labels. Do not point this at a process that handles secrets: a buffer can contain cookies, tokens, passwords, or request bodies. An unbounded group such as `path` or `.*` is a cardinality leak. Filter with `comm`, keep the regex tight, keep `max` small, and set `max_series`. Count mode never copies these buffers.

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
| `bpf_probe_events_total` | counter | `probe` and the argument labels of a custom kprobe or uprobe in count mode |
| `bpf_us_fn_events_total` | counter | capture mode, unless `metric` sets another name. Labels: `probe`, `comm`, and regex named groups |
| `bpf_probe_latency_seconds` | histogram | `probe` and the argument labels of a custom kprobe or uprobe with `latency;` |
| `bpf_probe_dropped_total` | counter | `probe`, `reason` (`lru`, `trunc`, `ringbuf_full`, `user_read_fail`, `stash_miss`, `regex_nomatch`, `regex_fail`) |
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
