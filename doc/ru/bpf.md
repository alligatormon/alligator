# eBPF collectors

**Language / Язык:** [English](../bpf.md) | [Русский](bpf.md)

Контекст `bpf` подключает kprobe, uprobe и набор built-in probes, затем публикует события как метрики. Программы остаются attached между scrape. Maps count и latency читаются на каждом scrape. События capture снимаются с ring buffer по мере поступления.

Это не меняет `perf_events_total`, `perf_events_scaling_ratio` и `perf_kernel_samples_total` — они остаются на `perf_event_open`. Счётчики сокетов и TCP states — на sock_diag. Имена host PSI метрик не меняются.

## Требования

На Linux collectors собираются по умолчанию для любого типа сборки (Debug, Release, RelWithDebInfo, MinSizeRel). Нужны `clang`, `bpftool`, статический `libbpf.a` (system/pkg-config) и Conan `elfutils/0.195` для статического `libelf` (zlib приходит через этот граф). Alligator не линкует `/lib64/libbpf.so.1`. `-DENABLE_BPF=OFF` отключает сборку.

В runtime нужен kernel 5.8+ и читаемый `/sys/kernel/btf/vmlinux`. Custom kprobe и uprobe требуют 5.15. `lock_contention` требует 5.17 и пропускается на более старых ядрах.

Процессу нужны `CAP_BPF` и `CAP_PERFMON`, либо `CAP_SYS_ADMIN`. Это отдельно от `perf_event_paranoid` (см. [system.md](system.md)).

Если BTF нет, ядро слишком старое или load запрещён, alligator один раз пишет в лог, публикует `bpf_up 0` и продолжает остальные collectors. Без контекста `bpf` метрика `bpf_up` не публикуется.

## Возможности (и чем это не является)

Alligator BPF — это коллектор **Prometheus-метрик** (counters и histograms), а не continuous profiler. Он **не** собирает stack traces, flamegraphs, USDT и не пишет поток событий по pid / connection 4-tuple. «Процесс» на практике — 16-байтное имя задачи `comm`, не `pid`. Включайте только нужные probes; именованные hist/counter делят staging-таблицу на **4096** записей за scrape (`bpf_probe_dropped_total{reason="lru"}` при переполнении).

| Вид | Статус | Probes / метрики | Замечания |
| --- | --- | --- | --- |
| CPU profiling (куда уходит CPU) | **Нет** | — | Нет on-CPU sampling, нет стеков |
| Off-CPU (где задача ждёт) | **Частично** | `offcpu` → `bpf_offcpu_seconds`, `bpf_offcpu_switches_total` | Длительность + `state`; opt-in `label waker;` (риск кардинальности). Нет стеков / причины ожидания сверх `state` |
| Memory | **Частично** | `cgroup_memory`, `page_faults` (нужен `comm`), `oom_kill` | PSI + `memory.current` / узкий `memory.stat`; faults горячие — всегда фильтруйте `comm`. Не alloc/heap profiling |
| Lock contention | **Частично** | `lock_contention` (ядро ≥5.17) | Только kernel locks (label `type`). Не имена pthread/userspace mutex; грубый намёк — `syscall_latency` + `futex` |
| Scheduler | **Да (метрики)** | `runqueue_latency` / `sched_latency`, `offcpu` | У `runqueue_latency` нет cgroup labels |
| I/O | **Да (метрики)** | `block_io`, `process_io`, `syscall_latency`, `file_access` | Latency / байты / open — не полный request tracing |
| TCP | **Да (метрики)** | `tcp_retransmit`, `tcp_connect_latency`, `tcp_rtt`, `tcp_reset` (+ UDP/DNS) | По `comm` (опционально `dport`); не сниффер 4-tuple |

**Разрезы:** фильтр `comm name;` (до 8) или без него — весь хост; `cgroup_labels;` добавляет `cgroup` / `pod` / `namespace` / `container` где поддерживается (не у `runqueue_latency`).

Стартер latency (scheduling + block + TCP connect/RTT):

```
bpf {
    cgroup_labels;
    probe runqueue_latency;
    probe offcpu;
    probe block_io;
    probe tcp_connect_latency;
    probe tcp_rtt;
}
```

Стартер memory / contention (`page_faults` узко; `label waker;` не включать без нужды):

```
bpf {
    cgroup_labels;
    probe offcpu;
    probe lock_contention;
    probe cgroup_memory;
    probe page_faults { comm nginx; }
    probe oom_kill;
}
```

Как читать histograms (границы ~1µs…~1s, затем `+Inf`):

```promql
histogram_quantile(0.99, sum by (le, comm) (rate(bpf_offcpu_seconds_bucket[5m])))
rate(bpf_lock_contentions_total[5m])
rate(bpf_page_faults_total{type="major"}[5m])
rate(bpf_tcp_retransmits_total[5m])
```

Справочник probes, labels и жёстких ограничений — в [Built-in probes](#built-in-probes) ниже.

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
    probe udp_activity { comm dns; }
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
    probe page_faults { comm nginx; }
    probe oom_kill;

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

`mode capture;` вешает отдельные capture-программы (`generic_kprobe_capture`, `generic_kretprobe_capture`, `generic_uprobe_capture`, `generic_uretprobe_capture`), а не программы count и latency. Каждый вызов копирует один буфер в ring buffer на 1MiB. Userspace гоняет PCRE1 по этому буферу. Именованные группы становятся Prometheus labels. Регулярное выражение не компилируется в eBPF, поток байт не склеивается: каждый `read`/`write` — отдельное событие. Кусок сообщения, который не совпал, отбрасывается.

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
- `size=retval` — длина в коде возврата (только `kretprobe` / `uretprobe`)
- `size=deref:argN` — длина это `u64` по указателю в аргументе N
- `size=deref:saved_argN` — то же, указатель сохранён на входе
- `max=N` — не больше N байт, жёсткий потолок 512. Более длинный источник увеличивает `bpf_probe_dropped_total{reason="trunc"}`, событие всё равно уходит
- `from=saved` — указатель буфера из entry stash
- `label` — имя для `regex on=...`. Сырой буфер label не становится
- `type=user_buffer` отклоняется, если у probe нет `mode capture`

`regex on=LABEL pattern="..."` вызывает `pcre_exec` с длиной скопированного буфера, не через `strlen`. Нет совпадения — событие сброшено (`reason="regex_nomatch"`). Шаблон, который не скомпилировался (в том числе из‑за одинаковых имён named groups без `PCRE_DUPNAMES`), сбрасывает все события этого probe (`reason="regex_fail"`); в логе будет `bpf: probe '...' regex failed`. До шести именованных групп становятся labels. У серии также есть `probe` и `comm`. У `kretprobe` / `uretprobe` метка `probe` — это `symbol:ret` или `binary:symbol:ret`. Без `metric` имя — `bpf_us_fn_events_total`.

`replace LABEL from=to …;` переписывает значение named group после матча и до создания серии. PCRE всегда захватывает байты из буфера (например `$`) и сам другую строку не подставит. Незамапленные значения остаются как есть. Можно повторять `replace` или перечислить несколько `from=to` в одной строке (всего до 16). Только capture.

```
    regex on=buf pattern="^(?P<type>[+*$:-])";
    replace type $=bulk +=simple :=int *=array -=error;
```

Без `replace` разнесите трафик по разным метрикам отдельными probes и pattern без named group (на recv у каждой пары свой `pair_id`, иначе stash столкнётся):

```
    kprobe __sys_recvfrom { mode capture; pair_id redis_bulk; save arg1; }
    kretprobe __sys_recvfrom {
        mode capture; pair_id redis_bulk; metric redis_reply_bulk_total; comm redis-cli;
        when retval_gt 0;
        arg 1 type=user_buffer size=retval max=128 label=buf from=saved;
        regex on=buf pattern="^\$";
    }
    kprobe __sys_recvfrom { mode capture; pair_id redis_err; save arg1; }
    kretprobe __sys_recvfrom {
        mode capture; pair_id redis_err; metric redis_reply_error_total; comm redis-cli;
        when retval_gt 0;
        arg 1 type=user_buffer size=retval max=128 label=buf from=saved;
        regex on=buf pattern="^-";
    }
```

`kprobe` / `uprobe` в capture могут читать user pointer на входе. Если буфер заполняется только при возврате (`read` / `recv` / `recvfrom`), спарьте entry probe с `save` указателя и `kretprobe` (ядро) или `uretprobe` (userspace) с `from=saved` и `size=retval`. `latency;` на capture probe игнорируется.

Конфиг без `mode capture` не загружает capture-программы: ошибка верификатора capture не блокирует count и latency. В смешанном конфиге, если capture-программы не загрузились, эти probes пропускаются (`bpf_probe_skipped`), а count и latency всё равно attach. Если ring buffer capture не открылся, capture-ссылки снимаются и эти probes пропускаются. Они не остаются без потребителя.

Ring buffer 1MiB. Userspace читает его постоянно: libuv опрашивает fd ring buffer и за один wakeup забирает ограниченную пачку, а scrape дочитывает остаток. Переполнение — `reason="ringbuf_full"`. Ошибка `bpf_probe_read_user` — `reason="user_read_fail"`. Return просил сохранённый указатель, а stash нет — `reason="stash_miss"`. У capture своя таблица на 4096 комбинаций labels. Дальше новые комбинации отбрасываются и растёт `reason="lru"`. Уже созданные счётчики не сбрасываются и не выглядят так, будто серия началась заново с 1.

Если у namespace bpf нет явного `max_series`, capture ставит 4096. Явное значение, включая `0` (без лимита), сохраняется. Этот предел не даёт capture раздувать дерево метрик без границы.

Именованные группы regex становятся Prometheus labels. Не направляйте это на процесс с секретами: в буфере могут быть cookies, токены, пароли и тела запросов. Неограниченная группа вроде `path` или `.*` — утечка кардинальности. Фильтруйте `comm`, держите regex узким, `max` маленьким и задайте `max_series`. Count mode эти буферы не копирует.

### Plaintext TCP capture (Redis, Memcached)

Пример `SSL_write` / `SSL_read` выше работает только потому, что OpenSSL даёт plaintext на границе шифрования. Redis и Memcached обычно — **plaintext TCP** (Redis `6379`, Memcached `11211`); `SSL_write` там нет. Ловим байты, которые процесс кладёт в сокет, через `kprobe` на kernel-функцию с обычным C ABI: буфер — второй аргумент, длина — третий — та же раскладка `arg 1 type=user_buffer size=arg2`, что у `SSL_write`.

Берите `__sys_sendto` (и, если есть, `__sys_send` или `vfs_write`). **Не** используйте `sys_write`, `sys_send`, `sys_sendto` и `__x64_sys_*`: на x86_64 эти обёртки принимают `struct pt_regs *`, а alligator читает `PT_REGS_PARM1..6` как обычные C-аргументы без syscall-unwrap, поэтому `arg 1` / `arg 2` — не `buf` / `len`. Символ проверьте через `grep __sys_sendto /proc/kallsyms` (или `vfs_write`) и bpftrace до выката. `sys_sendmsg` / `sock_sendmsg` читать напрямую нельзя — payload в `msg->msg_iov`, а `user_buffer`-читатель его не разыменовывает.

```
bpf {
    kprobe __sys_sendto {
        mode capture;
        metric redis_cmd_total;
        comm redis-cli;
        arg 1 type=user_buffer size=arg2 max=128 label=buf;
        regex on=buf pattern="^\*\d+\r\n\$\d+\r\n(?P<cmd>GET|SET|DEL|HGET|HSET|INCR|DECR|EXISTS|PING|INFO|AUTH|TTL|EXPIRE)\r\n";
    }

    kprobe __sys_recvfrom {
        mode capture;
        pair_id redis_recvfrom;
        save arg1;
    }
    kretprobe __sys_recvfrom {
        mode capture;
        pair_id redis_recvfrom;
        metric redis_reply_total;
        comm redis-cli;
        when retval_gt 0;
        arg 1 type=user_buffer size=retval max=128 label=buf from=saved;
        regex on=buf pattern="^(?P<type>[+*$:-])";
        replace type $=bulk +=simple :=int *=array -=error;
    }

    kprobe vfs_write {
        mode capture;
        metric redis_reply_total;
        comm redis-server;
        arg 1 type=user_buffer size=arg2 max=128 label=buf;
        regex on=buf pattern="^(?P<type>[+*$:-])";
        replace type $=bulk +=simple :=int *=array -=error;
    }

    kprobe __sys_sendto {
        mode capture;
        metric memcached_cmd_total;
        comm python3;
        arg 1 type=user_buffer size=arg2 max=128 label=buf;
        regex on=buf pattern="^(?P<cmd>get|gets|set|add|replace|append|prepend|incr|decr|cas|touch|flush_all|version|stats|delete|quit)\b";
    }

    kprobe __sys_recvfrom {
        mode capture;
        pair_id mc_recvfrom;
        save arg1;
    }
    kretprobe __sys_recvfrom {
        mode capture;
        pair_id mc_recvfrom;
        metric memcached_reply_total;
        comm python3;
        when retval_gt 0;
        arg 1 type=user_buffer size=retval max=128 label=buf from=saved;
        regex on=buf pattern="^(?P<reply>VALUE|END|STORED|NOT_FOUND|NOT_STORED|OK|ERROR|DELETED|EXISTS|VERSION)\b";
    }
}

namespace {
    name bpf_tcp;
    max_series 4096;
}
```

**Направление.**

- **Исходящее** (send/write): kprobe на `__sys_sendto` / `vfs_write` — буфер валиден на entry.
  - **команды** (client → server): `comm` клиента + `__sys_sendto`,
  - **ответы** (server → client): `comm` сервера + `vfs_write` (или `__sys_sendto`, если сервер шлёт так).
- **Входящее** (recv/recvfrom/read): ядро копирует в user-буфер *до возврата*. Одного kprobe на entry мало — буфер пустой или старый. Спарьте `kprobe __sys_recvfrom` (`save arg1`) с `kretprobe __sys_recvfrom` (`from=saved`, `size=retval`, `when retval_gt 0`) — тот же stash, что у `SSL_read`, но на kernel-символе, который реально срабатывает у `redis-cli` (libc `uprobe`/`uretprobe` на `recvfrom` часто молчит). Берите `__sys_recvfrom`, не `__x64_sys_recvfrom` (та же проблема с `pt_regs *`, что на send).

Сначала проверьте bpftrace (`kprobe`+`kretprobe` на `__sys_recvfrom`, stash `arg1` на entry, `buf(uptr(...), retval)` на return). Ответы Redis начинаются с байта типа RESP: `$5\r\nvalue\r\n` (bulk), `:1\r\n` (int), `+OK\r\n` (simple) или `-WRONGTYPE …\r\n` / `-ERR …\r\n` (error). С `replace` выше это станет `type="bulk"|"int"|"simple"|"array"|"error"`. Без `-` в pattern ошибки только крутят `bpf_probe_dropped_total{reason="regex_nomatch"}`.

**Framing.** Один send/write может нести несколько pipelined-команд или только половину одной; Alligator поток не склеивает. Pattern привязан к `^`, поэтому он подписывает **первую** команду в буфере и игнорирует остальное. Половинная команда в начале сбрасывает событие (`reason="regex_nomatch"`). Поэтому pipelined Redis занижает счёт, а pipelined-ответы не дробятся на per-message события.

**Клиенты Redis шлют RESP.** `redis-cli` и большинство библиотек (go-redis, redis-py, hiredis) используют array-форму `*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n`. Ловите её (как в примере). ASCII/inline-pattern вроде `^GET\b` RESP пропускает и даёт только `regex_nomatch`.

**Memcached — ASCII.** Команды вида `get key\r\n` / `set key …\r\n`; lowercase-pattern выше подходит. Binary protocol здесь не разбирается. Пример — та же пара `__sys_sendto` / `__sys_recvfrom`, что у Redis; берите клиент с `recv`/`recvfrom` (например `python3`), не классический `nc` на TCP (часто читает через `vfs_read`).

**Фильтруйте `comm`.** `__sys_sendto` и особенно `vfs_write` срабатывают на каждый подходящий send/write процесса — другие сокеты, log-файлы, stdout. Без `comm` ловится всё. `comm` — 16-байтное имя задачи; `redis-cli` (9), `redis-server` (12), `memcached` (9) и `redis-sentinel` (14) влезают; более длинное имя клиента — его 15-символьное усечение.

**Секреты.** Эти pattern ловят только глагол (или байт типа RESP), не ключ и не значение, поэтому пароль Redis `AUTH` или секрет `CONFIG SET` в label не попадают. Не добавляйте группу, хватающую значение; неограниченная `path` / `.*` — утечка кардинальности и секрета.

**TLS.** Если соединение по TLS (Redis `tls-port`, Memcached через stunnel), используйте пример `SSL_write` / `SSL_read` выше с теми же pattern вместо socket kprobe.

**Символ.** Смотрите `/proc/kallsyms` на целевом хосте. `__sys_sendto` — обычный send-путь для `redis-cli`; `__sys_recvfrom` — парный receive (entry `save` + `kretprobe`). Если клиент ходит через `send` / `write` / `recv` / `read`, попробуйте `__sys_send` / `vfs_write` / `__sys_recv` / `vfs_read` (те же слоты буфера/длины у C ABI helpers; `vfs_*` ещё и на file/pipe I/O). Это надёжнее libc `uprobe`/`uretprobe` — многие клиенты до динамического libc не доходят.

Для полных метрик Redis/Memcached (hit/miss, `INFO`, `stats`) у alligator есть активные collectors `redis` и `memcached`, которые диалятся в порт и парсят ответ; capture — это пассивный per-message счёт по сети, не замена им.

## Built-in probes

Built-in probes публикуют **counters и histograms по `comm`**, не audit log и не поток сырых событий. Серии не создаются на pid, argv или connection 4-tuple.

`comm` — 16-байтное имя задачи (15 символов + NUL).

`comm name;` внутри probe оставляет только это имя. Повторять можно до 8 имён. Без `comm` пишутся все процессы.

Обязательные ограничения:

- `fd_track` **обязан** иметь хотя бы один `comm`; иначе probe отклоняется.
- `page_faults` **обязан** иметь хотя бы один `comm`; иначе probe отклоняется. Хук горячий — всегда фильтруйте.
- `file_access` без prefix allowlist (`prefix /etc;` или `filter=prefix value=/etc;`) **ничего не считает**.

```
bpf {
    probe offcpu { comm nginx; comm searchd; }
    probe tcp_retransmit { comm nginx; label dport; }
    probe fd_track { comm nginx; }
    probe page_faults { comm nginx; }
    probe file_access { prefix /etc; }
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
    probe softirq_latency;
    probe cfs_throttling;
}
```

- `runqueue_latency` — от wakeup до следующего запуска на CPU. Histogram `bpf_runqueue_latency_seconds`. Без cgroup label: wakeup идёт в контексте waking task.
- `sched_latency` — тот же интервал и тот же histogram; второй серии не добавляет.
- `offcpu` — время вне CPU до следующего запуска. Histogram `bpf_offcpu_seconds` и counter `bpf_offcpu_switches_total`. Labels: `comm`, `state` (`sleep`, `disk_sleep`, `parked` или `other` из `prev_state`). Опционально `label waker;` добавляет `waker` (comm задачи, сделавшей wakeup, из `sched_wakeup` / `sched_wakeup_new`, best-effort; без wakeup → `unknown`). BPF-карта `last_waker` обновляется только если у woken pid уже есть старт в `off_ts` и он проходит allowlist `comm` для offcpu; `sched_switch` удаляет запись для `next` даже без `off_ts` (HASH, не LRU). **Риск кардинальности:** каждый уникальный waker `comm` умножает серии; по умолчанию выключен. С `cgroup_labels` cgroup — у задачи, ушедшей с CPU. Exiting task не пишется.
- `block_io` — от issue block request до completion. Histogram `bpf_block_io_latency_seconds` с `dev` (`major:minor`) и `op` (`read`, `write`, `other`).
- `lock_contention` — ожидание kernel lock. Histogram `bpf_lock_contention_seconds` и counter `bpf_lock_contentions_total` по `comm` и `type`. Приоритет `type`, если выставлено несколько LCB flags: `mutex` > `spin` > `rwlock` (READ|WRITE) > `rt` > `percpu` > `other`. Поддерживает `cgroup_labels`. Label адреса/символа lock нет. На ядрах старше 5.17 probe пропускается, `bpf_probe_skipped{probe="lock_contention"}` = 1.
- `softirq_latency` — latency softirq raise→entry и entry→exit (tp_btf `softirq_raise` / `entry` / `exit`). Histograms `bpf_softirq_entry_latency_seconds` и `bpf_softirq_service_latency_seconds` с label `kind` (`hi`, `timer`, `net_tx`, `net_rx`, `block`, `irq_poll`, `tasklet`, `sched`, `hrtimer`, `rcu`). Без `comm`. ~10 kinds × buckets — низкая кардинальность. Пропускается, если BTF softirq traces недоступны.
- `cfs_throttling` — длительность CFS bandwidth throttle. fentry `throttle_cfs_rq` сохраняет `bpf_ktime_get_ns()` по ключу `cfs_rq*`; fentry `unthrottle_cfs_rq` пишет delta в histogram `bpf_cfs_throttling_seconds`. Используется monotonic BPF time на обоих концах (не `rq->clock` / `throttled_clock`), чтобы не занижать интервал при stale runqueue clock. С `cgroup_labels;` cgroup id берётся на старте throttle. LRU start map (4096). Пропуск, если любой fentry не load/attach (`bpf_probe_skipped{probe="cfs_throttling"}`).

Включение `offcpu` (особенно с `label waker;`) вместе с `lock_contention`, `page_faults` и `cgroup_memory` делит staging-таблицу на 4096 серий и может поднять `bpf_probe_dropped_total{reason="lru"}`.

### TCP, UDP и DNS

Адрес или порт становятся label только если probe это запросил.

```
bpf {
    probe tcp_retransmit { label dport; }
    probe tcp_connect_latency;
    probe tcp_rtt;
    probe tcp_reset { label dport; }
    probe tcp_accept_latency;
    probe tcp_syn_backlog;
    probe udp_activity { comm dns; }
    probe udp_rcvbuf_drops;
    probe kfree_skb;
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
- `tcp_accept_latency` — ожидание в accept-**queue**: от `inet_csk_reqsk_queue_add` (готовый child на listen queue, ключ — child sock) до успешного **return** `inet_csk_accept` (kretprobe; NULL игнорируется). Работает для blocking и non-blocking `accept()`. Histogram `bpf_tcp_accept_latency_seconds` с label listen `port` (на enqueue). Не SYN-hash (`inet_csk_reqsk_queue_hash_add` не используется). Обычный map lookup+delete (без helpers только для 5.13+); на общем минимуме BPF (ядро ≥5.8, custom kprobe ≥5.15 как везде). LRU start map 16384; серии — cap 4096. Пропуск, если любой attach не удался (`bpf_probe_skipped{probe="tcp_accept_latency"}`).
- `tcp_syn_backlog` — распределение `sk_ack_backlog` на `tcp_v4_syn_recv_sock` / `tcp_v6_syn_recv_sock`. Histogram **`bpf_tcp_syn_backlog`** (без суффикса `_seconds`). Те же exp2 buckets, что у latency, но значение — **размер** backlog: `le` = `1`, `2`, `4`, … (не секунды), `_sum` — сумма глубин. Не интерпретируйте `le` как время.
- `udp_activity` — UDP send и receive через `udp_sendmsg` / `udp_recvmsg` и IPv6-аналоги. Counter `bpf_udp_activity_total` по `comm` и `event` (`send` или `recv`). `send` считается на entry; `recv` — kretprobe и только при return value `> 0`, так что non-blocking `-EAGAIN` poll не попадает в счётчик. Это не захват пакетов и без labels адреса или порта.
- `udp_rcvbuf_drops` — `tp_btf/udp_fail_queue_rcv_skb`. Counter `bpf_udp_rcvbuf_drops_total{port}`. Порты ≥ 32768 схлопываются в `port="0"`. Пропуск без tracepoint.
- `kfree_skb` — таксономия дропов `tp_btf/kfree_skb`. Counter `bpf_kfree_skb_total{eth_proto,ip_proto,reason}`. **Без** port и IP. Считается каждый дроп, в том числе L3-only / stack-local skb без MAC header (например rcvbuf fail → `kfree_skb_reason`); `eth_proto` / `ip_proto` — best-effort (`other`, если заголовков нет). `eth_proto`: `ipv4`/`ipv6`/`other`; `ip_proto`: `tcp`/`udp`/`icmp`/`other`; `reason` — **числовой** код `skb_drop_reason` десятичной строкой (без статической таблицы имён: enum сдвигается между ядрами). Декодировать offline по `dropreason.h` / BTF хоста. Пропуск без tracepoint.
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

### Cgroup memory и page faults

```
bpf {
    probe cgroup_memory;
    probe page_faults { comm nginx; }
    probe oom_kill;
}
```

`cgroup_memory` читает cgroupfs. BPF-программу не attach'ит. Host PSI метрики не трогает. Тот же обход читает `memory.current` и фиксированное подмножество `memory.stat`, если файлы есть.

- `bpf_cgroup_memory_pressure_waiting_seconds_total`
- `bpf_cgroup_memory_pressure_stalled_seconds_total`
- `bpf_cgroup_memory_pressure_waiting_avg_percent{window="10|60|300"}`
- `bpf_cgroup_memory_pressure_stalled_avg_percent{window="10|60|300"}`
- `bpf_cgroup_memory_current_bytes`
- `bpf_cgroup_memory_stat_bytes{stat="anon|file|kernel|sock|shmem|slab"}` (gauges; только байтовые поля)
- `bpf_cgroup_memory_stat_total{stat="pgfault|pgmajfault"}` (counters)

Точный список ключей `memory.stat`: `anon`, `file`, `kernel`, `sock`, `shmem`, `slab`, `pgfault`, `pgmajfault`. Остальные ключи в файле игнорируются.

Labels: `cgroup`, и при Kubernetes path — `pod`, `namespace`, `container`.

- `page_faults` — `kretprobe` на `handle_mm_fault`. **Нужен хотя бы один `comm`.** Counter `bpf_page_faults_total` с `type` `minor` или `major` и `comm` (плюс cgroup labels при включении). Ошибки с `VM_FAULT_ERROR` не считаются. Горячий путь — всегда фильтруйте через `comm`. Не суммируйте с cgroup `pgfault`/`pgmajfault` (`bpf_cgroup_memory_stat_total`): per-task BPF и cgroupfs — разные источники и не совпадут. При неудачном attach — `bpf_probe_skipped{probe="page_faults"}`; без `comm` probe не включается.
- `oom_kill` — `tracepoint/oom/mark_victim`. Counter `bpf_oom_kills_total` по `comm` (плюс cgroup при включении). Пропускается, если tracepoint отсутствует.

## Cgroup labels

`cgroup_labels;` добавляет `cgroup`, `pod`, `namespace`, `container` к scheduling, block, lock, TCP, process, page-fault, OOM и `cfs_throttling` сериям, когда cgroup id резолвится. Id — inode каталога cgroup. Путь берётся из ограниченного обхода cgroupfs и из cgroup, которые уже видел cadvisor. Клиента kubelet нет.

`runqueue_latency` эти labels не получает. Если id известен, а путь нет — label равен числовому cgroup id.

## Метрики

Histograms — Prometheus histograms: `_bucket` с `le` в секундах, `_sum` в секундах, `_count`, **кроме** `bpf_tcp_syn_backlog`, где `le` / `_sum` — размеры backlog (exp2: 1, 2, 4, …). Latency границы: 1µs, 2µs, 4µs, … до ~1s, затем `+Inf`.

| Metric | Type | Labels |
| --- | --- | --- |
| `bpf_up` | gauge | нет. `1` пока programs attached, `0` если контекст задан, но ничего не attached |
| `bpf_probe_events_total` | counter | `probe` и labels аргументов custom kprobe/uprobe в count mode |
| `bpf_us_fn_events_total` | counter | capture mode, если `metric` не задал другое имя. Labels: `probe`, `comm` и именованные группы regex |
| `bpf_probe_latency_seconds` | histogram | `probe` и labels аргументов custom kprobe/uprobe с `latency;` |
| `bpf_probe_dropped_total` | counter | `probe`, `reason` (`lru`, `trunc`, `ringbuf_full`, `user_read_fail`, `stash_miss`, `regex_nomatch`, `regex_fail`) |
| `bpf_probe_skipped` | gauge | `probe`. `1`, если probe не attached (старое ядро, load/attach failed). Ошибка одного custom probe не выгружает остальные |
| `bpf_runqueue_latency_seconds` | histogram | `comm` |
| `bpf_offcpu_seconds` | histogram | `comm`, `state`, optional `waker` (`label waker;`), плюс cgroup labels при включении |
| `bpf_offcpu_switches_total` | counter | те же labels, что у `bpf_offcpu_seconds` |
| `bpf_block_io_latency_seconds` | histogram | `comm`, `dev`, `op` |
| `bpf_lock_contention_seconds` | histogram | `comm`, `type`, плюс cgroup labels при включении |
| `bpf_lock_contentions_total` | counter | те же labels, что у `bpf_lock_contention_seconds` |
| `bpf_softirq_entry_latency_seconds` | histogram | `kind` |
| `bpf_softirq_service_latency_seconds` | histogram | `kind` |
| `bpf_cfs_throttling_seconds` | histogram | optional cgroup labels при `cgroup_labels;` |
| `bpf_page_faults_total` | counter | `comm`, `type` (`minor`/`major`), optional cgroup |
| `bpf_oom_kills_total` | counter | `comm`, optional cgroup |
| `bpf_tcp_retransmits_total` | counter | `comm`, optional `dport` |
| `bpf_tcp_connect_latency_seconds` | histogram | `comm` |
| `bpf_tcp_rtt_seconds` | histogram | `comm` |
| `bpf_tcp_resets_total` | counter | `comm`, optional `dport` |
| `bpf_tcp_accept_latency_seconds` | histogram | `port` |
| `bpf_tcp_syn_backlog` | histogram | нет (`le` — размер backlog) |
| `bpf_dns_latency_seconds` | histogram | `comm`, optional `name` |
| `bpf_syscall_latency_seconds` | histogram | `comm`, имя syscall |
| `bpf_process_events_total` | counter | `comm`, `event` |
| `bpf_process_fds` | gauge | `comm`, `event` (`fd`) |
| `bpf_process_io_bytes_total` | counter | `comm`, `event` (`read` или `write`) |
| `bpf_exec_total` | counter | `comm`, `parent`, optional `path` |
| `bpf_file_access_total` | counter | `comm`, `path` |
| `bpf_net_activity_total` | counter | `comm`, `event` |
| `bpf_udp_activity_total` | counter | `comm`, `event` (`send` или `recv`) |
| `bpf_udp_rcvbuf_drops_total` | counter | `port` (ephemeral → `0`) |
| `bpf_kfree_skb_total` | counter | `eth_proto`, `ip_proto`, `reason` (decimal `skb_drop_reason`; без port/IP) |
| `bpf_priv_change_total` | counter | `comm`, `changed` |
| `bpf_cgroup_memory_pressure_*` | counter и gauge | `cgroup`, `pod`, `namespace`, `container`, `window` на averages |
| `bpf_cgroup_memory_current_bytes` | gauge | `cgroup`, `pod`, `namespace`, `container` |
| `bpf_cgroup_memory_stat_bytes` | gauge | то же плюс `stat` |
| `bpf_cgroup_memory_stat_total` | counter | то же плюс `stat` (fault-подобные поля) |

Collector не attach'ит USDT, не собирает stack traces и не загружает BPF object из конфигурации.
