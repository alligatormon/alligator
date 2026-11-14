# eBPF collectors

**Language / Язык:** [English](../bpf.md) | [Русский](bpf.md)

Контекст `bpf` подключает kprobe, uprobe и набор built-in probes, затем публикует события как метрики. Программы остаются attached между scrape. Maps count и latency читаются на каждом scrape. События capture снимаются с ring buffer по мере поступления.

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

`latency;` (JSON `"latency": true`) измеряет runtime функции через entry + return probe (`kretprobe` / `uretprobe`). Filters только на entry: неудачный filter не сохраняет timestamp и не увеличивает counter. При успехе counter растёт на entry, а return пишет histogram `bpf_probe_latency_seconds` с теми же labels. Start timestamps ключуются по `(tid, probe_id)`. Без `latency;` — только counter на entry. Без `mode` или с `mode count;` работает этот путь. `mode capture;` latency не пишет.

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

## Capture mode

`mode capture;` вешает отдельные capture-программы (`generic_kprobe_capture`, `generic_uprobe_capture`, `generic_uretprobe_capture`), а не программы count и latency. Каждый вызов копирует один буфер в ring buffer на 1MiB. Userspace гоняет PCRE1 по этому буферу. Именованные группы становятся Prometheus labels. Регулярное выражение не компилируется в eBPF, поток байт не склеивается: каждый `read`/`write` — отдельное событие. Кусок сообщения, который не совпал, отбрасывается.

Это не HTTP- и не TLS-парсер. OpenSSL ниже — пример разницы между аргументами на входе и буфером на возврате. Строки `curl -v` вроде `GET / HTTP/2` — логические; при HTTP/2 в `SSL_write` / `SSL_read` лежат client connection preface и бинарные фреймы (SETTINGS, HEADERS с HPACK и т.д.), а не ASCII request/status line.

Шаблоны на write ниже ловят либо HTTP/2 client preface (`PRI * HTTP/2.0…`, label `h2`), либо строку запроса HTTP/1.x (`method`, `path`, `version`). На read — status line HTTP/1.x либо тот же preface, если peer когда-либо шлёт его на чтении (серверы обычно не шлют; status ответа HTTP/2 в HPACK HEADERS и здесь не извлекается). SETTINGS, WINDOW_UPDATE, HEADERS и куски тела по-прежнему дают `regex_nomatch`. Имена named groups в одном pattern должны быть уникальны: повтор имени (например `http_version` в обеих альтернативах) ломает compile PCRE, и каждое событие становится `regex_fail`.

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

namespace {
    name bpf_usdt;
    max_series 4096;
}
```

В plain-конфиге кавычки сохраняют обратные слэши как написано. PCRE `\s` пишется `\s`. `{` внутри кавычек — часть шаблона, не новый блок. `/` в `HTTP/` экранировать не нужно.

`SSL_write(SSL *, buf, num)` копируется на входе: аргумент 1 — буфер, аргумент 2 — длина. Новые сборки curl/OpenSSL часто вызывают `SSL_write_ex` вместо `SSL_write`; для покрытия вешайте оба. `SSL_read` заполняет буфер до возврата, поэтому entry только делает `save` указателя, а `uretprobe` читает `min(retval, max)` байт. `SSL_write_ex` / `SSL_read_ex` передают `size_t *written` / `size_t *readbytes`; return читает сохранённый указатель (`deref:saved_arg3`, нативное 8-байтное целое). `when retval_gt 0` отбрасывает неположительный возврат до копирования. `pair_id` связывает stash входа с return. Ключ stash — `(tid, pair_id)`. Повторный вход той же пары в одном потоке перезаписывает регистры.

Поля `arg` для `type=user_buffer`:

- `size=argN` — длина в этом целочисленном аргументе
- `size=retval` — длина в коде возврата (только `uretprobe`)
- `size=deref:argN` — длина это `u64` по указателю в аргументе N
- `size=deref:saved_argN` — то же, указатель сохранён на входе
- `max=N` — не больше N байт, жёсткий потолок 512. Более длинный источник увеличивает `bpf_probe_dropped_total{reason="trunc"}`, событие всё равно уходит
- `from=saved` — указатель буфера из entry stash
- `label` — имя для `regex on=...`. Сырой буфер label не становится
- `type=user_buffer` отклоняется, если у probe нет `mode capture`

`regex on=LABEL pattern="..."` вызывает `pcre_exec` с длиной скопированного буфера, не через `strlen`. Нет совпадения — событие сброшено (`reason="regex_nomatch"`). Шаблон, который не скомпилировался (в том числе из‑за одинаковых имён named groups без `PCRE_DUPNAMES`), сбрасывает все события этого probe (`reason="regex_fail"`); в логе будет `bpf: probe '...' regex failed`. До шести именованных групп становятся labels. У серии также есть `probe` и `comm`. У uretprobe метка `probe` — это `binary:symbol:ret`. Без `metric` имя — `bpf_us_fn_events_total`.

`kprobe` в capture читает user pointer на входе. Return — только `uretprobe`. `latency;` на capture probe игнорируется.

Конфиг без `mode capture` не загружает capture-программы: ошибка верификатора capture не блокирует count и latency. В смешанном конфиге, если capture-программы не загрузились, эти probes пропускаются (`bpf_probe_skipped`), а count и latency всё равно attach. Если ring buffer capture не открылся, capture-ссылки снимаются и эти probes пропускаются. Они не остаются без потребителя.

Ring buffer 1MiB. Userspace читает его постоянно: libuv опрашивает fd ring buffer и за один wakeup забирает ограниченную пачку, а scrape дочитывает остаток. Переполнение — `reason="ringbuf_full"`. Ошибка `bpf_probe_read_user` — `reason="user_read_fail"`. Return просил сохранённый указатель, а stash нет — `reason="stash_miss"`. У capture своя таблица на 4096 комбинаций labels. Дальше новые комбинации отбрасываются и растёт `reason="lru"`. Уже созданные счётчики не сбрасываются и не выглядят так, будто серия началась заново с 1.

Если у namespace bpf нет явного `max_series`, capture ставит 4096. Явное значение, включая `0` (без лимита), сохраняется. Этот предел не даёт capture раздувать дерево метрик без границы.

Именованные группы regex становятся Prometheus labels. Не направляйте это на процесс с секретами: в буфере могут быть cookies, токены, пароли и тела запросов. Неограниченная группа вроде `path` или `.*` — утечка кардинальности. Фильтруйте `comm`, держите regex узким, `max` маленьким и задайте `max_series`. Count mode эти буферы не копирует.

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
| `bpf_probe_events_total` | counter | `probe` и labels аргументов custom kprobe/uprobe в count mode |
| `bpf_us_fn_events_total` | counter | capture mode, если `metric` не задал другое имя. Labels: `probe`, `comm` и именованные группы regex |
| `bpf_probe_latency_seconds` | histogram | `probe` и labels аргументов custom kprobe/uprobe с `latency;` |
| `bpf_probe_dropped_total` | counter | `probe`, `reason` (`lru`, `trunc`, `ringbuf_full`, `user_read_fail`, `stash_miss`, `regex_nomatch`, `regex_fail`) |
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
