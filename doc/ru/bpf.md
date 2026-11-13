# eBPF collectors

**Language / Язык:** [English](../bpf.md) | [Русский](bpf.md)

Контекст `bpf` подключает kprobe, uprobe и набор built-in probes, затем публикует события как метрики. Программы остаются attached между scrape; каждый scrape только читает maps.

Это не меняет `perf_events_total`, `perf_events_scaling_ratio` и `perf_kernel_samples_total` — они остаются на `perf_event_open`. Счётчики сокетов и TCP states — на sock_diag. Имена host PSI метрик не меняются.

## Требования

На Linux collectors собираются по умолчанию для любого типа сборки (Debug, Release, RelWithDebInfo, MinSizeRel). Нужны `clang`, `bpftool`, статический `libbpf.a` (system/pkg-config) и Conan `elfutils/0.195` для статического `libelf` (zlib приходит через этот граф). Alligator не линкует `/lib64/libbpf.so.1`. `-DENABLE_BPF=OFF` отключает сборку.

В runtime нужен kernel 5.8+ и читаемый `/sys/kernel/btf/vmlinux`. Custom kprobe и uprobe требуют 5.15. `lock_contention` требует 5.17 и пропускается на более старых ядрах.

Процессу нужны `CAP_BPF` и `CAP_PERFMON`, либо `CAP_SYS_ADMIN`. Это отдельно от `perf_event_paranoid` (см. [system.md](system.md)).

Если BTF нет, ядро слишком старое или load запрещён, alligator один раз пишет в лог, публикует `bpf_up 0` и продолжает остальные collectors. Без контекста `bpf` метрика `bpf_up` не публикуется.

## Конфигурация

Probe выключен, пока его нет в списке. Контекст целиком заменяет предыдущую bpf-конфигурацию.

Полный пример built-in probes и latency uprobe (prefixes для `file_access` — в одном probe; один и тот же probe не дублировать):

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

Короткий starter: scheduling, block I/O и TCP connect/RTT:

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

Тот же объект через JSON API:

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

Удалите ключ `bpf`, чтобы отключить всё.

## Namespace

Серии попадают в default namespace. Задайте `namespace`, чтобы публиковать их отдельно, и ограничьте этот namespace, если labels могут расти:

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

`namespace custom;` и `namespace=custom;` эквивалентны. Без директивы серии остаются в default namespace. Label `namespace` на cgroup-серии — это Kubernetes namespace, не эта настройка.

## Custom kprobe/uprobe

До шести аргументов. У каждого — type, label и необязательный filter. Filter выполняется до записи события. Значение, не прошедшее filter, отбрасывается и серию не создаёт.

Типы:

- `u64` — целое, в decimal
- `user_str` — userspace string, не больше 64 байт
- `kernel_str` — kernel string, не больше 64 байт
- `sockaddr` — IPv4 или IPv6 адрес, либо порт при label `dport` / `port`

Filters: `eq`, `prefix`, `suffix`, `allow`. `allow` можно повторять на одном аргументе. Labels `path`, `filename`, `file`, `uid` без filter отклоняются.

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
```

Каждый настроенный аргумент должен пройти свой filter (AND). Для нескольких значений предпочтительнее один `allow=` на аргументе.

`arg 1` у `tcp_v4_connect` — kernel `sockaddr` (уже скопирован из userspace через `move_addr_to_kernel`). Alligator читает его через `bpf_probe_read_kernel` на kprobe и `bpf_probe_read_user` на uprobe. Поле `host`/`daddr` — числовой адрес (например `10.1.2.3`), не DNS-имя; для фильтра по hostname используйте string-аргумент на `getaddrinfo`.

События, прошедшие filter, увеличивают `bpf_probe_events_total`. Labels: `probe` плюс labels аргументов. Pid, полный command line и raw address не становятся labels, пока их не настроили как аргумент. У каждой generic probe map — 4096 комбинаций labels. Именованные histogram/counter от всех built-in probes делят одну userspace staging table на 4096 записей за scrape. Сверх лимита новые комбинации увеличивают `bpf_probe_dropped_total{probe,reason}`, а самая старая запись переиспользуется. Строка длиннее 64 байт учитывается как truncation на той же метрике.

`latency;` (JSON `"latency": true`) измеряет runtime функции через entry + return probe (`kretprobe` / `uretprobe`). Filters только на entry: неудачный filter не сохраняет timestamp и не увеличивает counter. При успехе counter растёт на entry, а return пишет histogram `bpf_probe_latency_seconds` с теми же labels. Start timestamps ключуются по `(tid, probe_id)`. Без `latency;` — только counter на entry.

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

JSON для одного kprobe:

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

`comm` — 16-байтное имя задачи (15 символов + NUL). Эти probes не создают серию на pid, argv или connection 4-tuple.

`comm name;` внутри probe оставляет только это имя. Повторять можно до 8 имён. Без `comm` пишутся все процессы. Исключение — `fd_track`: без хотя бы одного `comm` probe отклоняется.

```
bpf {
    probe offcpu { comm nginx; comm searchd; }
    probe tcp_retransmit { comm nginx; label dport; }
    kprobe tcp_v4_connect { comm nginx; arg 1 type=sockaddr label=daddr filter=prefix value=10.; }
}
```

### Scheduling и block I/O

```
bpf {
    probe runqueue_latency;
    probe offcpu;
    probe block_io;
    probe lock_contention;
}
```

- `runqueue_latency` — от wakeup до следующего запуска на CPU. Histogram `bpf_runqueue_latency_seconds`. Без cgroup label: wakeup идёт в контексте waking task.
- `sched_latency` — тот же интервал и тот же histogram; второй серии не добавляет.
- `offcpu` — время вне CPU до следующего запуска. Histogram `bpf_offcpu_seconds`. С `cgroup_labels` cgroup — у задачи, ушедшей с CPU. Exiting task не пишется.
- `block_io` — от issue block request до completion. Histogram `bpf_block_io_latency_seconds` с `dev` (`major:minor`) и `op` (`read`, `write`, `other`).
- `lock_contention` — ожидание kernel lock. Histogram `bpf_lock_contention_seconds` по `comm`. Label имени lock нет. На ядрах старше 5.17 probe пропускается, `bpf_probe_skipped{probe="lock_contention"}` = 1.

### TCP и DNS

Адрес или порт становятся label только если probe это запросил.

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

- `tcp_retransmit` — counter `bpf_tcp_retransmits_total`. `label dport;` добавляет destination port.
- `tcp_connect_latency` — от `SYN_SENT` до established или `CLOSE`. Histogram `bpf_tcp_connect_latency_seconds`.
- `tcp_rtt` — smoothed RTT один раз при established. Histogram `bpf_tcp_rtt_seconds`.
- `tcp_reset` — отправленные и полученные TCP reset. Counter `bpf_tcp_resets_total`. `label dport;` добавляет destination port, когда он доступен.
- `dns_latency` — `getaddrinfo` в указанном binary. По умолчанию `/lib64/libc.so.6`. Histogram `bpf_dns_latency_seconds`. Имя запроса — label только при совпадении с `filter`. Остальные lookups остаются в unlabeled серии и не считаются дважды. Имя хранится не больше 31 байта.

### Processes

```
bpf {
    probe syscall_latency { syscall read; syscall write; syscall openat; }
    probe process_lifecycle;
    probe fd_track { comm nginx; comm worker; }
    probe process_io;
}
```

- `syscall_latency` — время enter→exit для перечисленных syscalls. Histogram `bpf_syscall_latency_seconds` с именем syscall. Без строк `syscall` список: `read`, `write`, `openat`, `futex`, `poll`, `connect`. Подключить все syscalls сразу нельзя.
- `process_lifecycle` — fork, exec, exit. Counter `bpf_process_events_total` по `comm` и `event`.
- `fd_track` — open минус close для указанных `comm`. Gauge `bpf_process_fds` с `comm` и `event="fd"`. Без `comm` probe отклоняется.
- `process_io` — байты `read`/`write` по `comm` или cgroup, не по pid. Counter `bpf_process_io_bytes_total` с `event` `read` или `write`.

### Exec, files, network, credentials

Это counters, не audit log и не копия файла или пакета.

```
bpf {
    probe exec { filter=prefix value=/usr/local/bin; }
    probe file_access { prefix /etc; prefix /var/lib; }
    probe net_activity;
    probe priv_change;
}
```

- `exec` — `bpf_exec_total` по `comm` и parent `comm`. Совпадение с `filter` добавляет filename как `path`. Без filter label `path` нет.
- `file_access` — путь `openat`. Counter `bpf_file_access_total`. Пока нет prefix allowlist (`prefix /etc;` или `filter=prefix value=/etc;`), ничего не считается. `eq`, `suffix`, `allow` для этого probe отклоняются.
- `net_activity` — `connect` и `accept`. Counter `bpf_net_activity_total` по `comm` и `event`.
- `priv_change` — credential commit. Counter `bpf_priv_change_total` по `comm` и `changed` (`0` или `1`). uid не label.

### Cgroup memory pressure

```
bpf {
    probe cgroup_memory;
}
```

Читает `memory.pressure` из cgroupfs. BPF-программу не attach'ит. Host PSI метрики не трогает.

- `bpf_cgroup_memory_pressure_waiting_seconds_total`
- `bpf_cgroup_memory_pressure_stalled_seconds_total`
- `bpf_cgroup_memory_pressure_waiting_avg_percent{window="10|60|300"}`
- `bpf_cgroup_memory_pressure_stalled_avg_percent{window="10|60|300"}`

Labels: `cgroup`, и при Kubernetes path — `pod`, `namespace`, `container`.

## Cgroup labels

`cgroup_labels;` добавляет `cgroup`, `pod`, `namespace`, `container` к scheduling, block, TCP и process сериям, когда cgroup id резолвится. Id — inode каталога cgroup. Путь берётся из ограниченного обхода cgroupfs и из cgroup, которые уже видел cadvisor. Клиента kubelet нет.

`runqueue_latency` эти labels не получает. Если id известен, а путь нет — label равен числовому cgroup id.

## Метрики

Histograms — Prometheus histograms: `_bucket` с `le` в секундах, `_sum` в секундах, `_count`. Границы: 1µs, 2µs, 4µs, … до ~1s, затем `+Inf`.

| Metric | Type | Labels |
| --- | --- | --- |
| `bpf_up` | gauge | нет. `1` пока programs attached, `0` если контекст задан, но ничего не attached |
| `bpf_probe_events_total` | counter | `probe` и labels аргументов custom kprobe/uprobe |
| `bpf_probe_latency_seconds` | histogram | `probe` и labels аргументов custom kprobe/uprobe с `latency;` |
| `bpf_probe_dropped_total` | counter | `probe`, `reason` (`lru` или truncation) |
| `bpf_probe_skipped` | gauge | `probe`. `1`, если probe не attached (старое ядро, load/attach failed). Ошибка одного custom probe не выгружает остальные |
| `bpf_runqueue_latency_seconds` | histogram | `comm` |
| `bpf_offcpu_seconds` | histogram | `comm`, плюс cgroup labels при включении |
| `bpf_block_io_latency_seconds` | histogram | `comm`, `dev`, `op` |
| `bpf_lock_contention_seconds` | histogram | `comm` |
| `bpf_tcp_retransmits_total` | counter | `comm`, optional `dport` |
| `bpf_tcp_connect_latency_seconds` | histogram | `comm` |
| `bpf_tcp_rtt_seconds` | histogram | `comm` |
| `bpf_tcp_resets_total` | counter | `comm`, optional `dport` |
| `bpf_dns_latency_seconds` | histogram | `comm`, optional `name` |
| `bpf_syscall_latency_seconds` | histogram | `comm`, имя syscall |
| `bpf_process_events_total` | counter | `comm`, `event` |
| `bpf_process_fds` | gauge | `comm`, `event` (`fd`) |
| `bpf_process_io_bytes_total` | counter | `comm`, `event` (`read` или `write`) |
| `bpf_exec_total` | counter | `comm`, `parent`, optional `path` |
| `bpf_file_access_total` | counter | `comm`, `path` |
| `bpf_net_activity_total` | counter | `comm`, `event` |
| `bpf_priv_change_total` | counter | `comm`, `changed` |
| `bpf_cgroup_memory_pressure_*` | counter и gauge | `cgroup`, `pod`, `namespace`, `container`, `window` на averages |

Collector не attach'ит USDT, не собирает stack traces и не загружает BPF object из конфигурации.
