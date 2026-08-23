**Language / Язык:** [English](../configuration.md) | [Русский](configuration.md)

# Конфигурация

## Индекс документации

| Тема | Документ |
|-------|----------|
| Сбор метрик (pull) | [aggregate.md](aggregate.md) |
| Приём метрик (push) и scrape | [entrypoint.md](entrypoint.md) |
| Метрики хоста | [system.md](system.md) |
| PromQL / SQL запросы | [query.md](query.md) |
| Экспорт и автоматизация | [action.md](action.md), [scheduler.md](scheduler.md) |
| Пространства имён метрик | [namespace.md](namespace.md) |
| Разбор логов | [grok.md](grok.md), [mtail/README.md](mtail/README.md) |
| VRL (avrl) remap / metrics / DNS builtins | [vrl/README.md](vrl/README.md) |
| Пользовательские модули | [lang.md](lang.md) |
| Проверки DNS resolution | [resolver.md](resolver.md) |
| Мониторинг TLS-сертификатов | [x509.md](x509.md) |
| Репликация кластера | [cluster.md](cluster.md) |
| Service discovery | [service-discovery.md](service-discovery.md) |
| HTTP API | [api.md](api.md) |
| Blackbox probe modules | [probe.md](probe.md) |
| Puppeteer / browser checks | [puppeteer.md](puppeteer.md), [chromecdp.md](chromecdp.md) |
| Kubernetes operator | [kubernetes-operator.md](kubernetes-operator.md) |
| Thread pools | [threaded-loop.md](threaded-loop.md) |
| Логирование для разработчиков (`glog` / `carglog`) | [logging.md](logging.md) |

### Контексты конфигурации верхнего уровня

Эти блоки могут появляться в `/etc/alligator.conf` (plain) или JSON-конфигурации:

| Контекст | Назначение |
|---------|---------|
| `entrypoint` | Адреса прослушивания, handlers, scrape/export endpoints |
| `aggregate` | Poll внешних targets и запуск parsers |
| `system` | Сбор метрик хоста и контейнеров |
| `query` | PromQL или SQL по расписанию |
| `action` | Экспорт метрик или запуск команд |
| `scheduler` | Trigger actions по интервалу |
| `resolver` | DNS servers для blackbox/resolver checks |
| `namespace` | Объявление префиксов metric namespace |
| `grok` | Регистрация grok pattern sets |
| `mtail` | Регистрация mtail programs |
| `vrl` | Регистрация avrl programs (`dns_lookup` / `reverse_dns`, negative DNS cache) |
| `lang` | Загрузка external language modules |
| `probe` | Prometheus-style probe modules (`GET /probe`); см. [probe.md](probe.md) |
| `x509` | Мониторинг certificate files |
| `cluster` / `instance` | Cluster membership |
| `puppeteer` | Headless browser automation (Node.js Puppeteer) |
| `chromecdp` | Метрики браузера через Chrome DevTools Protocol (без Node.js) |
| `threaded_loop` | Именованные worker thread pools |
| `enrichment_table` | Таблицы CSV / MaxMind для VRL; см. [vrl/README.md](vrl/README.md) |
| `persistence` | Сохранение метрик на диск между перезапусками |
| `modules` | Сопоставление имён и путей `.so` для parsers и `lang so` |

### Глобальные директивы

Задаются вне контекстных блоков (plain) или как ключи верхнего уровня JSON:

| Директива | Назначение |
|-----------|------------|
| `log_level`, `log_dest`, `log_channel`, `log_form`, `log_time`, `log_time_format` | Логирование по умолчанию и именованные каналы |
| `ttl` | Глобальный TTL метрик (секунды) |
| `aggregate_period` | Интервал scrape для `aggregate` |
| `system_collect_period` | Интервал сбора метрик хоста |
| `tls_collect_period` | Интервал файлового коллектора `x509` |
| `query_period` | Интервал внутренних query |
| `synchronization_period` | Интервал синхронизации кластера |
| `workers` | Размер thread pool libuv (`auto` или число) |
| `metrictree_hashfunc` | Хеш дерева метрик: `lookup3`, `murmur`, `crc32`, `XXH3` |
| `process_shell` | Shell для process spawner (по умолчанию `/bin/sh`) |
| `persistence` | `{ directory, period }` — каталог и интервал сохранения метрик |

Синтаксис интервалов — в разделе [единицы времени](#available-units-for-time-data-in-configuration-file).

## Доступные уровни логирования
- off
- fatal
- error
- warn
- info
- debug
- trace

## Назначения логов
```
log_dest <dest>;
```

Назначение может быть standard streams Unix ОС, файл, UDP-порт или TCP socket. Например, директива может быть задана так:
```
- stdout
- stderr
- file:///var/log/messages
- udp://127.0.0.1:514
- tcp://127.0.0.1:1514
- http://127.0.0.1:9200/alligator-logs/_bulk
- kafka://127.0.0.1:9092/alligator-logs
```

Kafka destinations (`kafka://brokers/topic`) публикуют логи асинхронно через `librdkafka`. Используйте их только на именованных записях `log_channel`; глобальный default channel `log_dest` не меняется, пока вы явно его не настроите.

Формат broker list: `host:port` или comma-separated `host1:port1,host2:port2`. Имя topic — URI path.
Можно задать optional `kafka_key` и `kafka_options` per channel или передать options в URI query parameters.

Kafka channels non-blocking. Если internal producer queue полон или broker недоступен, log lines drop-ятся и throttled diagnostic пишется в stderr.

### Метрики log channel shipper

Alligator экспортирует counters (via `system_carg`) для каждой записи log channel:

| Metric | Labels | Значение |
|--------|--------|---------|
| `alligator_log_channel_sent_total` | `channel`, `kind` | accepted by sink (queued/written) |
| `alligator_log_channel_dropped_total` | `channel`, `kind`, `reason` | intentionally dropped |
| `alligator_log_channel_errors_total` | `channel`, `kind`, `reason` | produce/serialize failure |

`kind`: `raw` (`log_channel_raw`), `out` (transformed `log_channel_out`), `diag` (`carglog` / `glog`).

Примеры `reason`: `queue_full`, `disconnected`, `busy`, `produce`, `serialize`, `oom`.

```promql
rate(alligator_log_channel_dropped_total{kind="out"}[5m])
  /
clamp_min(
  rate(alligator_log_channel_sent_total{kind="out"}[5m])
  + rate(alligator_log_channel_dropped_total{kind="out"}[5m]),
  1e-9)
```

TCP log destinations подключаются асинхронно через libuv. Если remote side не connected или write in progress, log lines drop-ятся (`reason=disconnected|busy`). Reconnection retry в background каждые несколько секунд.

Когда и URI query parameters, и `kafka_options` присутствуют, значения `kafka_options` имеют приоритет.

Рекомендуемые formats для Kafka consumers: `log_format json` или `log_format elastic`.

```json
{
  "log_channel": [
    {
      "name": "kafka-aggregate",
      "dest": "kafka://127.0.0.1:9092/alligator-aggregate-logs?key=aggregate",
      "kafka_options": {
        "acks": "all",
        "compression.type": "lz4",
        "linger.ms": 20
      },
      "log_format": "json",
      "log_time": true
    }
  ],
  "aggregate": [
    {
      "url": "tcp://127.0.0.1:9100",
      "handler": "prometheus",
      "log_channel": "kafka-aggregate"
    }
  ]
}
```

TCP log destinations подключаются асинхронно через libuv. Если remote side не connected или write in progress, log lines drop-ятся. Reconnection retry в background каждые несколько секунд.

HTTP destinations (`http://host:port/path`) POST logs в Elasticsearch-compatible endpoints через bulk NDJSON protocol (`Content-Type: application/x-ndjson`). HTTP channels по умолчанию `log_format elastic`.

Для Logstash TCP inputs с JSON codec используйте `tcp://` с `log_format elastic` — каждая log line JSON document, terminated newline.

`log_format json` пишет один JSON object per line с `message`, `key` (из context `carg->key`) и `date` (channel timestamp). Работает с любым destination: stdout, file, UDP, TCP.

Optional per-channel fields для remote logging:

- `log_format` — `plain` (default для TCP/stdout/file), `json`, или `elastic` / `elasticsearch` / `ecs`
- `log_index` — Elasticsearch index name template, strftime-compatible (default `alligator-%Y.%m.%d`)
- `kafka_key` — partition key для Kafka destination
- `kafka_options` — object с `librdkafka` producer options (`name: value`)
  - JSON config: `"kafka_options": {"acks":"all","linger.ms":20}`
  - Plain config: повторите `kafka_options key:value;` внутри `log_channel { ... }`

```json
{
  "log_channel": [
    {
      "name": "elk-http",
      "dest": "http://127.0.0.1:9200/alligator-logs/_bulk",
      "log_index": "alligator-%Y.%m.%d"
    },
    {
      "name": "logstash-tcp",
      "dest": "tcp://127.0.0.1:5044",
      "log_format": "elastic"
    },
    {
      "name": "json-file",
      "dest": "file:///var/log/alligator.json.log",
      "log_format": "json",
      "log_time": true
    }
  ]
}
```

## Log channels

Именованные log channels маршрутизируют context logs в разные destinations. Global `log_dest` остаётся default channel.

```json
{
  "log_dest": "stdout",
  "log_channel": [
    {"name": "aggregate", "dest": "file:///var/log/alligator-aggregate.log"},
    {"name": "system", "dest": "udp://127.0.0.1:514", "log_time": true}
  ],
  "aggregate": [
    {
      "url": "tcp://127.0.0.1:9100",
      "handler": "prometheus",
      "log_channel": "aggregate"
    }
  ]
}
```

Каждый channel принимает те же destination values, что `log_dest`. Optional per-channel fields: `log_form`, `log_time`, `log_time_format`.

Context logs через `carglog` prefixed `[context_key]` или `[channel_name/context_key]` при named channel.

### Raw stream passthrough (`log_channel_raw`)

На **entrypoint** и **aggregate** contexts, читающих user data из **files или sockets** (`file://`, `tcp://`, `udp://`, `unix://`, `unixgram://`, `tls://`), `log_channel_raw` forwards incoming payload в named channel **line by line** (split на `\n`; trailing `\r` strip). Bytes без trailing newline buffer-ятся до next read. **Message body не переписывается** (без `carglog`-style `[channel/key]` prefix); channel settings только добавляют outer envelope:

- `log_format plain` + `log_time off` — каждая line отдельным write (payload без newline)
- `log_format plain` + `log_time on` — channel timestamp prefix, затем one line per write
- `log_format json` — один JSON object per line с `message` (payload), optional `key`, и `date`
- `log_format elastic` — ECS-style document per line с `@timestamp`, `message`, и metadata (HTTP destinations use bulk NDJSON)

Используйте как extra sink рядом с grok/mtail metric parsing или с `handler log` для log shipping без metrics.

`log_channel` (diagnostic logs via `carglog`) и `log_channel_raw` (incoming user payload) независимы.

### Transformed log sink (`log_channel_out`)

После remap **VRL / grok / amtail** alligator может ship **transformed** events в
named channel. Отдельно от `log_channel_raw` (passthrough input bytes).

| Option | Payload |
|--------|---------|
| `log_channel_raw` | original stream, physical lines |
| `log_channel_out` | remapped events (explicit operator per engine) |

**VRL (расширение Alligator, не Vector VRL):** только explicit `.log` / `.logs`.
Flat objects пишутся как flat JSON documents; strings — то же wrapping, что
`log_channel_write_raw`. Metrics via `.metrics` могут run на той же record.

```conf
log_channel {
    name pg-json;
    dest kafka://127.0.0.1:9092/pg-parsed;
    log_format json;
}

aggregate {
    vrl "file:///var/log/postgresql.csv" name=postgresql_csv
        log_channel_out=pg-json
        start_pattern='^\d{4}-\d{2}-\d{2}'
        condition_pattern='^\s'
        multiline_mode=continue_through;
}
```

Оба sink могут быть заданы сразу (raw audit + structured). В большинстве случаев предпочитайте один.

**amtail:** вызовите `emit_log("...")` или `emit_log($capture)` внутри script (расширение Alligator).
Без вызова → no log в `log_channel_out` (metrics всё ещё work).

**grok:** при successful match, если `log_channel_out` set, emit one flat JSON document
с named captures плюс `message` (matched line). Metrics unchanged.

```json
{
  "log_channel": [
    {
      "name": "kafka-raw",
      "dest": "kafka://127.0.0.1:9092/alligator-raw-logs",
      "kafka_key": "syslog",
      "log_format": "json",
      "log_time": true
    }
  ],
  "entrypoint": [
    {
      "handler": "log",
      "tcp": "1514",
      "log_channel_raw": "kafka-raw"
    }
  ],
  "aggregate": [
    {
      "url": "file:///var/log/app.log",
      "handler": "log",
      "log_channel_raw": "kafka-raw"
    }
  ]
}
```

Plain config (log-only forwarding):

```conf
entrypoint {
    handler log;
    tcp 1514;
    log_channel_raw kafka-raw;
}

aggregate {
    log "file:///var/log/app.log" log_channel_raw=kafka-raw;
}
```

С metrics parsing (grok/mtail) на том же stream:

```json
  "entrypoint": [
    {
      "handler": "grok",
      "grok": "syslog",
      "tcp": "1514",
      "log_channel_raw": "kafka-raw"
    }
  ],
  "aggregate": [
    {
      "url": "file:///var/log/app.log",
      "handler": "mtail",
      "mtail": "app.mtail",
      "log_channel_raw": "kafka-raw"
    }
  ]
```

Plain:

```conf
entrypoint {
    handler grok;
    grok syslog;
    tcp 1514;
    log_channel_raw kafka-raw;
}

aggregate {
    mtail "file:///var/log/app.log" handler=mtail mtail=app.mtail log_channel_raw=kafka-raw;
}
```

На aggregate URL lines используйте `log_channel_raw=name`; внутри `entrypoint` blocks — `log_channel_raw name;`.

### Plain configuration: per-context log files

Определите один channel per context, каждый пишет в свой file под `/var/log/`, затем reference channel из того context:

```conf
# Global fallback log (startup, config parser, uncategorized messages)
log_level info;
log_dest file:///var/log/alligator.log;
log_time on;

# Channel definitions (one file per area)
log_channel {
    name aggregate;
    dest file:///var/log/alligator-aggregate.log;
    log_time on;
}

log_channel {
    name system;
    dest file:///var/log/alligator-system.log;
    log_time on;
}

log_channel {
    name entrypoint;
    dest file:///var/log/alligator-entrypoint.log;
    log_time on;
}

log_channel {
    name action;
    dest file:///var/log/alligator-action.log;
    log_time on;
}

log_channel {
    name remote-tcp;
    dest tcp://127.0.0.1:1514;
    log_time on;
}

log_channel {
    name elk;
    dest http://127.0.0.1:9200/alligator-logs/_bulk;
    log_index alligator-%Y.%m.%d;
    log_time on;
}

log_channel {
    name json-log;
    dest file:///var/log/alligator.json.log;
    log_format json;
    log_time on;
}

log_channel {
    name kafka-aggregate;
    dest kafka://127.0.0.1:9092/alligator-aggregate-logs;
    kafka_key aggregate;
    kafka_options acks:all;
    kafka_options compression.type:lz4;
    kafka_options linger.ms:20;
    log_format json;
    log_time on;
}

# --- contexts: pick channel + verbosity ---

system {
    base;
    network;
    log_level debug;
    log_channel system;
}

entrypoint {
    log_level debug;
    log_channel entrypoint;
    tcp 9100;
    handler prometheus;
}

aggregate {
    prometheus "http://127.0.0.1:9100/metrics" log_level=debug log_channel=aggregate;
    redis "tcp://localhost:6379" log_level=info log_channel=aggregate;
}

action {
    name export-metrics;
    serializer prometheus;
    expr http://127.0.0.1:9100/metrics;
    log_level trace;
    log_channel action;
}
```

`dest` и `log_dest` equivalent внутри `log_channel` block. На aggregate URL lines — `log_channel=name` и `log_channel_raw=name`; внутри `system`, `entrypoint`, `action` blocks — `log_channel name;` и `log_channel_raw name;`.

Query и другие contexts, логирующие via global `glog()`, всё ещё используют `log_dest` (default channel).

## Доступные единицы времени в файле конфигурации:
- `ms` — миллисекунды
- `s` — секунды (голое число в строке тоже секунды)
- `m` — минуты
- `h` — часы
- `d` — дни
- `w` — недели

Примеры: `timeout 5s;`, `period 1m;`, `dns_timeout 2s;`, `"ttl": "1h"`.
JSON integers для millisecond fields — raw milliseconds (`2000` == `"2000ms"`).
См. также [vrl/README.md](vrl/README.md) для VRL DNS duration options.
## Комментарии в plain config
Plain configuration parser поддерживает single-line и multiline comments:
- `# comment text` для single-line comments.
- `/* comment text */` для multiline comments.


## /etc/alligator.conf
Ниже пример структуры configuration file для Alligator:
```
log_level <level>;
ttl <time>;

entrypoint {
    <options>;
}

resolver {
    <server1>;
    <server2>;
    ...
    <serverN>;
}

system {
    <option1>;
    <option2>;
    ...
    <optionN> [<param1>] ... [<paramN>];
}

aggregate {
    <option1>;
    <option2>;
    ...
    <optionN>;
}

query {
    <query1 options>;
}

query {
    <query2 options>;
}

action {
    <action1 options>;
}

action {
    <action2 options>;
}

scheduler {
    <scheduler1 options>;
}

scheduler {
    <scheduler2 options>;
}

x509 {
    <certificate options>;
}

x509 {
    <certificate options>;
}

puppeteer {
    <puppeteer options>;
}
```

<a id="support-environment-variables"></a>

# Поддержка переменных окружения
`__` — separator вложенности contexts
Пример:
```
export ALLIGATOR__ENTRYPOINT0__TCP0=1111
export ALLIGATOR__ENTRYPOINT0__TCP1=1112
export ALLIGATOR__TTL=1200
export ALLIGATOR__LOG_LEVEL=0
export ALLIGATOR__AGGREGATE0__HANDLER=tcp
export ALLIGATOR__AGGREGATE0__URL="tcp://google.com:80"
```
преобразуется в configuration:
```
{
  "entrypoint": [
    {
      "tcp": [
        "1111",
        "1112"
      ]
    }
  ],
  "ttl": "1200",
  "log_level": "0",
  "aggregate": [
    {
      "handler": "tcp",
      "url": "tcp://google.com:80"
    }
  ]
}
```
