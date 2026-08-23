**Language / Язык:** [English](../aggregate.md) | [Русский](aggregate.md)

# Aggregator
Функция aggregator даёт возможность собирать метрики из другого ПО.\
Она состоит из двух основных частей: `aggregator` и `parser`.

<br>
<p align="center">
<h1 align="center" style="border-bottom: none">
    <img alt="alligator-cluster-entrypoint" src="../images/aggregator.jpeg"></a><br>
</h1>
<br>

Event loop Alligator предоставляет асинхронные методы для сбора метрик из различных источников и передачи их пользовательским parser'ам для каждого ПО:
<br>
<h1 align="center" style="border-bottom: none">
    <img alt="alligator-cluster-aggregate" src="../images/parsers.jpeg"></a><br>
</h1>

<br>
<br>
</p>

Aggregator включает асинхронные методы для получения статистики по различным схемам/протоколам:
- HTTP (http://) и HTTPS (https://). Включает HTTP- и HTTPS-клиентов для получения body.
- TCP (tcp://). Включает TCP-клиент для получения body.
- TLS (tls://). Включает TLS-клиент для получения body.
- UDP (udp://). Включает UDP-клиент для получения body.
- unix (unix://) и unixgram (unixgram://). Включает клиентов Unix-socket поверх SOCK\_STREAM и SOCK\_DGRAM для получения body.
- file (file://). Включает чтение файла для получения body.
- exec (exec://). Включает выполнение внешней программы и передачу stdout в parser.
- WebSocket (ws://) и WebSocket over TLS (wss://). Включает постоянного WebSocket-клиента, передающего каждый полученный text frame в parser.

Parser получает body после работы aggregator и разбирает его в метрики.

## Oneshot vs await

Постоянные scrape'ы `aggregate` остаются на callback-клиенте: таймер crawl подключается, выполняется handler parser'а, сессия закрывается, следующий период повторяется. Это подходящая модель для множества параллельных HTTP/TCP-целей.

`aggregator_oneshot()` — тот же транспорт для одного запроса (actions, VRL `http_request`, fan-out parser `try_again`, exec). Завершение — callback parser'а.

`aggregator_oneshot_await()` — последовательный стиль на том же клиенте: запускает oneshot, затем крутит общий libuv loop (`uva_await`), пока не выполнится handler или путь пустого сбоя. Используйте для коротких последовательных fetch'ей (OCSP, список peer'ов k8s, Redis `KEYS`, затем `MGET`).

Ограничения (вложенный `uv_run`):

- Вызывать только из потока loop.
- Не await'ить из callback `uv_close`.
- Не await'ить на том же `context_arg`, который сейчас в connect/read/close.
- Глубина вложенного await ограничена (по умолчанию 8). OCSP во время TLS handshake уже занимает один уровень.
- Для параллельного fan-out оставляйте `try_again` (CouchDB по базам, пространства имён Aerospike). Await сериализует.

HTTPS, unix sockets, exec, DNS Alligator и scrape-метрики доступны при await, потому что это клиент aggregator, а не второй HTTP-стек.

## Overview
```
aggregate {
    <parser1> <url1> [arg1] [arg2] ... [argN];
    <parser2> <url2> [arg1] [arg2] ... [argN];
    ...
    <parserM> <urlM> [arg1] [arg2] ... [argN];
}
```

## env
По умолчанию: -
Множественное: да

Задаёт HTTP-заголовки для протоколов HTTP/HTTPS или переменные окружения для запуска внешних процессов (exec).\
Пример использования:
```
aggregate {
    blackbox https://google.com env=Connection:close env=User-agent:alligator;
}
```

## key
По умолчанию: генерируется автоматически из URL
Множественное: нет

Переопределяет key — уникальную переменную, которая обычно генерируется автоматически и используется как первичный ключ в метриках, связанных с работой aggregator или API.


## bind\_address
По умолчанию: 0.0.0.0:{random port}, где random port выбирается ОС автоматически
Множественное: нет

Заставляет исходящие подключения к целевому серверу исходить с указанного локального адреса.
Поддерживаемые форматы:
- `bind_address=<port>` или `bind_address=:<port>` — bind только по локальному порту (IP по умолчанию `0.0.0.0`)
- `bind_address=<ip>` — bind только по локальному IP (порт выбирает ОС)
- `bind_address=<ip>:<port>` — bind по локальному IP и локальному порту

Например:

```
aggregate {
    blackbox https://example.com bind_address=1234;
    blackbox https://example.com bind_address=:1234;
    dns udp://8.8.8.8:53 resolve=google.com type=a add_label=check:dns bind_address=0.0.0.0;
    dns udp://8.8.4.4:53 resolve=yahoo.com type=a add_label=check:dns bind_address=192.0.2.1:1234;
}
```

## proxy
По умолчанию: -\
Множественное: нет

Отправляет aggregate через HTTP- или SOCKS5-прокси. URL scrape (и labels метрик, `Host`, TLS SNI) остаются origin; меняется только TCP/UDP hop.

Схемы:

- `proxy=http://[user:pass@]host[:port]` — прокси HTTP без шифрования (порт по умолчанию 8080). Для origin HTTPS/TLS/TCP используется `CONNECT`. Для origin `http://` без шифрования — запрос с absolute-URI (`GET http://origin/path`) с опциональным `Proxy-Authorization`. TLS к прокси (`https://proxy`) не поддерживается.
- `proxy=socks5://[user:pass@]host[:port]` или `socks5h://` — SOCKS5 (порт по умолчанию 1080). TCP использует CONNECT; UDP — UDP ASSOCIATE. Имена хостов origin отправляются прокси, если origin не IPv4-литерал. Username/password — RFC 1929.

HTTP/HTTPS-прокси не переносят UDP. Для aggregate `udp://` используйте SOCKS5.

```
aggregate {
    http https://example.com/metrics proxy=http://user:pass@proxy:3128;
    tcp  tcp://db:5432 proxy=socks5://127.0.0.1:1080;
    dns  udp://8.8.8.8:53 resolve=example.com type=a
         proxy=socks5h://127.0.0.1:1080;
}
```

## name
По умолчанию: -\
Множественное: нет

Задаёт имя текущего объекта aggregate. Это имя позволяет запросу ссылаться на текущий aggregator.


## lang
По умолчанию: -\
Множественное: нет

Опция [lang](https://github.com/alligatormon/alligator/blob/master/doc/lang.md) задаёт context, обрабатывающий полученный body пользовательскими функциями через внешние модули.

## follow\_redirects
По умолчанию: 0
Множественное: нет

Опция follow\_redirects задаёт максимальное число следований редиректам.

## tls\_certificate
По умолчанию: -\
Множественное: нет

Задаёт путь к клиентскому x509-сертификату для создания mTLS-сессий.

## tls\_key
По умолчанию: -\
Множественное: нет

Задаёт путь к клиентскому x509-ключу для создания TLS-сессий.


## tls\_ca
По умолчанию: -\
Множественное: нет

Задаёт путь к x509-сертификату центра сертификации (CA) для проверки этой TLS-сессии.


## tls\_server\_name
По умолчанию: -\
Множественное: нет

Задаёт поле TLS servername, когда это необходимо.

## tls\_verify
По умолчанию: off\
Множественное: нет

Включает проверку сертификата peer (`SSL_VERIFY_PEER`) для этого aggregate. Требуется для проверок CRL во время handshake.

## tls\_crl
По умолчанию: -\
Множественное: нет

Путь к PEM/DER CRL. Загружается в TLS store и используется `x509_cert_valid`.

## tls\_crl\_scope
По умолчанию: leaf\
Множественное: нет

`leaf` или `chain`. `chain` задаёт `X509_V_FLAG_CRL_CHECK_ALL`.

## tls\_ocsp
По умолчанию: off\
Множественное: нет

Включает OCSP. URL responder берётся из AIA сертификата, если не задан `tls_ocsp_responder`.

## tls\_ocsp\_responder
По умолчанию: -\
Множественное: нет

Переопределяет AIA URL OCSP `http://` или `https://`.

## tls\_ocsp\_proxy
По умолчанию: -\
Множественное: нет

HTTP- или SOCKS5-прокси только для fetch OCSP. Независимо от scrape `proxy=`. Те же URL-схемы, что у `proxy` (`http://`, `socks5://`, `socks5h://`). TLS к прокси (`https://proxy`) не поддерживается.

```
aggregate {
    https https://example.com tls_verify=on tls_ocsp=on
        tls_ocsp_proxy=http://user:pass@proxy:3128;
}
```

## tls\_ocsp\_stapling
По умолчанию: off\
Множественное: нет

Запрашивает у peer stapled OCSP response и проверяет её перед fallback на fetch.

## tls\_ocsp\_timeout
По умолчанию: 3s\
Множественное: нет

Таймаут HTTP-запроса OCSP.

## tls\_ocsp\_cache\_ttl
По умолчанию: 1h\
Множественное: нет

Максимальный TTL кэша OCSP (также ограничивается `nextUpdate`).

## tls\_revocation\_mode
По умолчанию: soft\
Множественное: нет

`soft`: только ответ revoked ломает handshake. `hard`: ошибки fetch/unknown тоже ломают.

## tls\_ocsp\_fetch
По умолчанию: background\
Множественное: нет

`background` мягко разрешает первое подключение и заполняет кэш для следующего. `inline` await'ит HTTP-запрос OCSP во время handshake через `aggregator_oneshot_await` (responder'ы http:// и https://).


## timeout
По умолчанию: 5s\
Множественное: нет

Задаёт таймаут запроса.
Подробнее о единицах времени в конфигурации — в [документации](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-units-for-time-data-in-configuration-file).


## calc\_lines
По умолчанию: false\
Множественное: нет

Включает подсчёт строк в файлах внутри каталога.


## checksum
По умолчанию: false\
Множественное: нет
Допустимые значения:
- murmur3
- crc32

Включает вычисление checksum для файлов внутри каталога.


## notify
По умолчанию: false\
Множественное: нет\
Допустимые значения:
- true
- false
- only

Включает механизмы inotify для отслеживания изменений файлов в каталоге. При `only` директива запускает чтение файлов только по уведомлениям и отключает глобальный планировщик file aggregator (`file_aggregator_repeat`). При `notify=false` (по умолчанию) новые байты файла подхватываются на каждом глобальном tick crawl файлов (`file_aggregator_repeat` в конфиге `system`, по умолчанию 10s); все pending-строки с последнего сохранённого offset читаются одним batch за tick и передаются построчно. Не путайте глобальный `file_aggregator_repeat` с per-aggregate `period` (отдельный таймер на файл).


## state
По умолчанию: stream\
Множественное: нет\
Возможные значения:
- begin
- save
- stream
- forget

Включает разные режимы начала чтения для Alligator — и при перезапуске, и во время работы:
- **begin**: при старте читает файл с начала, затем только добавленные данные.
- **save**: сохраняет offset файла между перезапусками Alligator и читает только добавленные данные.
- **stream**: начинает чтение с текущего конца файла и читает только добавленные данные.
- **forget**: всегда читает файл с начала.


## pingloop
По умолчанию: 0\
Множественное: нет\
Возможные значения:
- {number}

Pingloop позволяет blackbox handler'у пинговать ресурс более одного раза.


## log\_level
По умолчанию: off\
Множественное: нет

Задаёт уровень логирования для aggregator. Единицы для этой опции описаны в [документе](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-log-levels)

## log\_channel\_raw
По умолчанию: -\
Множественное: нет

Для транспортов file и socket (`file://`, `tcp://`, `udp://`, `unix://`, `unixgram://`, `tls://`) пересылает входящие данные **построчно** (newline-delimited) в именованный log channel. Каждая строка остаётся целой внутри `message`; `log_format` / `log_time` channel'а добавляют только JSON/elastic-обёртку или префикс timestamp. Используйте с `handler log` для чистой доставки логов или вместе с grok/mtail, когда метрики парсятся отдельно. См. [configuration — raw stream passthrough](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#raw-stream-passthrough-log_channel_raw).

Простой пример: `log "file:///var/log/app.log" log_channel_raw=kafka-raw;`


## log\_channel\_out
По умолчанию: -\
Множественное: нет

Sink для **преобразованных** log-событий после VRL / grok / amtail remap (не сырых байтов).
Независимо от `log_channel_raw`. VRL эмитит только когда скрипт явно задаёт
`.log` / `.logs`; объекты — плоские JSON-документы. См.
[configuration — transformed log sink](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#transformed-log-sink-log_channel_out)
и [vrl README](https://github.com/alligatormon/alligator/blob/master/doc/vrl/README.md#log-export-log--logs--log_channel_out).

Простой пример: `vrl "file:///var/log/app.log" name=app log_channel_out=pg-json;`


## threaded_loop_name
По умолчанию: -\
Множественное: нет

Задаёт объявленное имя threaded loop. Подробнее о threaded loop — в документации [threaded_loop](https://github.com/alligatormon/alligator/blob/master/doc/threaded-loop.md). Включает thread pool для конкретного aggregator и гарантирует, что main thread его не обрабатывает.


## stdin
Задаёт body, передаваемый в stdin вызываемого скрипта.


## cluster
По умолчанию: -\
Множественное: нет

Задаёт имя cluster для crawl метрик. Подробнее о cluster — в документации [cluster](https://github.com/alligatormon/alligator/blob/master/doc/cluster.md).

## instance
По умолчанию: -\
Множественное: нет

Задаёт имя текущего instance cluster для crawl метрик. Подробнее о cluster — в документации [cluster](https://github.com/alligatormon/alligator/blob/master/doc/cluster.md).


## pquery
По умолчанию: -\
Множественное: нет

Задаёт JSON-запрос для parser json\_query. Подробнее — в документации [json\_query](https://github.com/alligatormon/alligator/blob/master/doc/parsers/json_query.md).

## period
По умолчанию: -\
Множественное: нет

Задаёт интервал повторного вызова запроса.\
Подробнее о единицах времени в конфигурации — в [документации](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-units-for-time-data-in-configuration-file).


## resolve
По умолчанию: -\
Множественное: нет

Задаёт доменное имя для резолва. Подробнее о DNS-запросах в alligator — в документации [resolver](https://github.com/alligatormon/alligator/blob/master/doc/resolver.md).

## metricstransform
По умолчанию: -\
Множественное: нет

`metricstransform` переписывает **ключи и/или значения labels** для метрик, созданных этой строкой aggregate; результат сохраняется в Alligator так же, как transforms при ingest через entrypoint.
Формат правил — тот же OTel-style JSON, что и в других context'ах (`transforms` -> `operations` -> `value_actions`, плюс опциональные `new_label` / `label_key_actions` как в [action.md § metricstransform](https://github.com/alligatormon/alligator/blob/master/doc/action.md#metricstransform)).

В plain config передайте либо **inline JSON** (`metricstransform='{...}'` или `metricstransform={...}`), либо **native block** после handler и URL на той же строке aggregate.

Inline JSON:
```
aggregate {
    prometheus_metrics http://127.0.0.1:9100/metrics metricstransform='{"transforms":[{"include":"^node_.*$","match_type":"regexp","operations":[{"action":"update_label","label":"instance","value_actions":[{"regex":"^([^:]+):?.*$","replacement":"$1"}]}]}]}';
}
```

Native block (та же keyword-грамматика, что в [action.md § metricstransform](https://github.com/alligatormon/alligator/blob/master/doc/action.md#metricstransform)):
```
aggregate {
    prometheus_metrics http://127.0.0.1:9100/metrics metricstransform {
        include ^node_.*$ match_type regexp label instance regex '^([^:]+):?.*$' replacement '$1';
    };
}
```

Полезно для очистки cardinality до storage и до экспорта через actions/serializers.

Когда те же метрики позже экспортируются через **action** с `metric_name_transform`, [export-time `metricstransform`](https://github.com/alligatormon/alligator/blob/master/doc/action.md#matching-metric-names-include-metric-metric-regex) на этой action может совпадать либо с сохранённым именем, либо с преобразованным export-именем; правила на этапе aggregate здесь видят только имя, произведённое aggregate/parser.

## WebSocket transport (ws:// and wss://)

Схемы `ws://` и `wss://` подключаются к WebSocket-серверу и подписываются на его stream. Подключение **постоянное** — alligator поддерживает его непрерывно и автоматически переподключается, если сервер закрывает или рвёт socket.

Каждый text frame с сервера передаётся напрямую в настроенный parser так же, как body TCP-ответа. Любой parser, работающий поверх TCP, работает и поверх WebSocket.

Порты по умолчанию: `80` для `ws://`, `443` для `wss://`.

### Reconnect behaviour

| Ситуация | Поведение |
|-----------|-----------|
| Начальное подключение не удалось | Повтор через `period` (по умолчанию 10 s) |
| Сервер закрыл подключение | Переподключение через `period` |
| Сервер недоступен | Повтор через `period` |

Используйте `period` для интервала переподключения.

### Typical use cases

**Prometheus metrics endpoint через WebSocket** — полезно, когда цель за WebSocket-прокси или custom push-based exporter:

```
aggregate {
    prometheus_metrics ws://metrics-relay.internal:9100/metrics;
    prometheus_metrics wss://secure-relay.internal/metrics add_label=env:prod;
}
```

**Blackbox проверка связности и доступности** — сам WebSocket handshake выступает probe; alligator эмитит TCP/TLS timing-метрики, даже если frame'ы не приходят:

```
aggregate {
    blackbox ws://api.example.com:8080/health add_label=service:api;
    blackbox wss://ws.example.com/status      add_label=service:ws-gateway;
}
```

**Prometheus metrics с переписыванием labels и интервалом переподключения:**

```
aggregate {
    prometheus_metrics ws://node-exporter-relay:9091/stream
        period=30s
        add_label=datacenter:dc1
        metricstransform='{"transforms":[{"include":"^node_.*$","match_type":"regexp","operations":[{"action":"update_label","label":"instance","value_actions":[{"regex":"^([^:]+):?.*$","replacement":"$1"}]}]}]}';
}
```

**Несколько WebSocket-источников:**

```
aggregate {
    prometheus_metrics ws://relay-a.internal/metrics add_label=relay:a;
    prometheus_metrics ws://relay-b.internal/metrics add_label=relay:b;
    blackbox           wss://healthcheck.internal/ws  add_label=check:websocket;
}
```

### Notes

- Опция `period` задаёт и расписание сбора, и задержку переподключения после disconnect. Если опущена, задержка переподключения по умолчанию 10 s.
- WebSocket-клиент отправляет стандартный RFC 6455 upgrade handshake. Сервер должен ответить HTTP 101. Negotiation subprotocol не выполняется.
- `wss://` (TLS) разбирается и регистрируется; TLS-слой в roadmap — сейчас подключение идёт без TLS даже для URL `wss://`. Если нужно шифрование сегодня, поставьте TLS-terminating proxy перед целью.

## add_label
По умолчанию: -\
Множественное: да

Добавляет статические labels ко всем метрикам, созданным этой строкой aggregate. Формат: `add_label=key:value`.

```
aggregate {
    elasticsearch http://localhost:9200 add_label=instance:localhost add_label=name:localcluster;
    elasticsearch http://external:9200 add_label=instance:external add_label=name:extcluster;
}
```

Также поддерживается на entrypoints и actions. См. [entrypoint add_label](https://github.com/alligatormon/alligator/blob/master/doc/entrypoint.md#add_label).

## namespace
По умолчанию: -\
Множественное: нет

Назначает метрики этого aggregate в именованное пространство имён. См. [namespace](https://github.com/alligatormon/alligator/blob/master/doc/namespace.md).

## no_collect
По умолчанию: false\
Множественное: нет

Оставляет строку aggregate в конфигурации, но отключает сбор метрик.

## ttl
По умолчанию: глобальный `ttl`\
Множественное: нет

Переопределяет TTL метрик для этой строки aggregate.

## file_stat
По умолчанию: false\
Множественное: нет

При чтении файлов или каталогов экспортирует метаданные файлов (`file_stat_time`, `file_stat_size`, `file_stat_mode` и связанные семейства). Используется вместе с `blackbox file://` и похожими handler'ами.

## script
По умолчанию: -\
Множественное: нет

Shell-скрипт для aggregate `exec://`. Вывод передаётся в parser.

## Available parsers

Первый токен в строке `aggregate` — **handler key**, зарегистрированный в Alligator. Должен совпадать с кодом точно (например `named`, а не `bind`; `nginx_upstream_check`, а не `nginx`; `nvidia_smi`, а не `nvidia-smi`).

### Parser documentation

**Databases and caches:** [redis](https://github.com/alligatormon/alligator/blob/master/doc/parsers/redis.md), [memcached](https://github.com/alligatormon/alligator/blob/master/doc/parsers/memcached.md), [MongoDB](https://github.com/alligatormon/alligator/blob/master/doc/parsers/mongodb.md), [mysql](https://github.com/alligatormon/alligator/blob/master/doc/parsers/mysql.md), [postgresql](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md) ([pgbouncer](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md#pgbouncer), [odyssey](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md#odyssey), [pgpool](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md#pgpool)), [clickhouse](https://github.com/alligatormon/alligator/blob/master/doc/parsers/clickhouse.md), [cassandra](https://github.com/alligatormon/alligator/blob/master/doc/parsers/cassandra.md), [couchbase](https://github.com/alligatormon/alligator/blob/master/doc/parsers/couchbase.md), [couchdb](https://github.com/alligatormon/alligator/blob/master/doc/parsers/couchdb.md), [riak](https://github.com/alligatormon/alligator/blob/master/doc/parsers/riak.md), [aerospike](https://github.com/alligatormon/alligator/blob/master/doc/parsers/aerospike.md)

**Messaging and queues:** [rabbitmq](https://github.com/alligatormon/alligator/blob/master/doc/parsers/rabbitmq.md), [beanstalkd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/beanstalkd.md), [nats](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nats.md), [gearmand](https://github.com/alligatormon/alligator/blob/master/doc/parsers/gearmand.md)

**Web and proxies:** [haproxy](https://github.com/alligatormon/alligator/blob/master/doc/parsers/haproxy.md), [nginx](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nginx.md) (`nginx_upstream_check`), [apache httpd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/apache-httpd.md) (`httpd`), [lighttpd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/lighttpd.md), [uwsgi](https://github.com/alligatormon/alligator/blob/master/doc/parsers/uwsgi.md), [varnish](https://github.com/alligatormon/alligator/blob/master/doc/parsers/varnish.md), [squid](https://github.com/alligatormon/alligator/blob/master/doc/parsers/squid.md)

**Search and analytics:** [elasticsearch](https://github.com/alligatormon/alligator/blob/master/doc/parsers/elasticsearch.md), [druid](https://github.com/alligatormon/alligator/blob/master/doc/parsers/druid.md) (`druid`, `druid_broker`, `druid_historical`, `druid_worker`), [hadoop](https://github.com/alligatormon/alligator/blob/master/doc/parsers/hadoop.md), [opentsdb](https://github.com/alligatormon/alligator/blob/master/doc/parsers/opentsdb.md), [eventstore](https://github.com/alligatormon/alligator/blob/master/doc/parsers/eventstore.md)

**DNS:** [named](https://github.com/alligatormon/alligator/blob/master/doc/parsers/named.md) (`named`), [powerdns](https://github.com/alligatormon/alligator/blob/master/doc/parsers/powerdns.md), [gdnsd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/gdnsd.md), [unbound](https://github.com/alligatormon/alligator/blob/master/doc/parsers/unbound.md), [nsd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nsd.md), [dns](https://github.com/alligatormon/alligator/blob/master/doc/resolver.md) (resolver checks)

**Infrastructure:** [kubernetes](https://github.com/alligatormon/alligator/blob/master/doc/parsers/kubernetes.md) (`kubernetes_operator`, `kubernetes_endpoint`, `kubernetes_ingress`, `kubeconfig`), [keepalived](https://github.com/alligatormon/alligator/blob/master/doc/parsers/keepalived.md), [monit](https://github.com/alligatormon/alligator/blob/master/doc/parsers/monit.md), [patroni](https://github.com/alligatormon/alligator/blob/master/doc/parsers/patroni.md), [zookeeper](https://github.com/alligatormon/alligator/blob/master/doc/parsers/zookeeper.md), [sentinel](https://github.com/alligatormon/alligator/blob/master/doc/parsers/sentinel.md), [snmp](https://github.com/alligatormon/alligator/blob/master/doc/parsers/snmp.md), [ipmi](https://github.com/alligatormon/alligator/blob/master/doc/parsers/ipmi.md), [wazuh](https://github.com/alligatormon/alligator/blob/master/doc/parsers/wazuh.md), [ntp](https://github.com/alligatormon/alligator/blob/master/doc/parsers/ntp.md), [nvidia-smi](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nvidia-smi.md) (`nvidia_smi`)

**Storage:** [mogilefs](https://github.com/alligatormon/alligator/blob/master/doc/parsers/mogilefs.md), [moosefs](https://github.com/alligatormon/alligator/blob/master/doc/parsers/moosefs.md)

**Observability and logs:** [syslog-ng](https://github.com/alligatormon/alligator/blob/master/doc/parsers/syslog-ng.md), [auditd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/auditd.md) (entrypoint `auditd`), [rsyslog impstats](https://github.com/alligatormon/alligator/blob/master/doc/parsers/rsyslog.md) (entrypoint `rsyslog-impstats`), [prometheus_metrics](https://github.com/alligatormon/alligator/blob/master/doc/parsers/prometheus_metrics.md), [json_query](https://github.com/alligatormon/alligator/blob/master/doc/parsers/json_query.md), [blackbox](https://github.com/alligatormon/alligator/blob/master/doc/parsers/blackbox.md), [tftp](https://github.com/alligatormon/alligator/blob/master/doc/parsers/tftp.md)

**Other:** Celery [flower](https://github.com/alligatormon/alligator/blob/master/doc/parsers/flower.md), [consul](https://github.com/alligatormon/alligator/blob/master/doc/service-discovery.md) (`consul`, `consul-configuration`, `consul-discovery`), [lang](https://github.com/alligatormon/alligator/blob/master/doc/lang.md), [grok](https://github.com/alligatormon/alligator/blob/master/doc/grok.md), [mtail](https://github.com/alligatormon/alligator/blob/master/doc/mtail/README.md), **log** (только forward через `log_channel_raw`)

**Utility handlers (no dedicated parser page):** `http`, `tcp`, `process`, `influxdb` (scheduler export), `jsonparse`, `redis_ping`, `sphinxsearch`, `dummy`

## Example of usage with TLS options
```
aggregate {
    prometheus_metrics https://consulnode:8501/v1/agent/metrics?format=prometheus tls_ca=/etc/consul.d/ca.crt tls_certificate=/etc/consul.d/server.crt tls_key=/etc/consul.d/server.key tls_server_name=consulcluster env=X-Consul-Token:XXXX;
}
```

## Example of usage checking directory for changes:
```
aggregate {
    blackbox file:///etc/ calc_lines=true file_stat=true checksum=murmur3;
}
```

Эта конфигурация генерирует метрики для каждого файла в каталоге:
```
file_lines {path="/etc/fuse.conf"} 17
file_stat_time {type="birthtime", path="/etc/fuse.conf"} 1676453256
file_stat_time {type="mtime", path="/etc/fuse.conf"} 1648043594
file_stat_time {type="ctime", path="/etc/fuse.conf"} 1676453256
file_stat_mode {type="regular", user="root", path="/etc/fuse.conf", group="root", int="100644", mode="rw-r--r--"} 33188
file_stat_modify_count {path="/etc/fuse.conf"} 3
file_stat_size {path="/etc/fuse.conf"} 694
file_checksum {path="/etc/fuse.conf", hash="murmur3"} 1032033040
```

Обратите внимание: при разборе файлов в каталоге путь каталога должен заканчиваться на '/', например: `file:///var/log/`. Это включает режим обхода каталога. По умолчанию включена проверка одного файла.

### Glob / match (basename filter)

Чтобы читать только файлы, совпадающие с шаблоном basename, либо укажите glob в basename URL (`*`, `?`, `[...]`), либо задайте `match=` / `glob=` на URL каталога. Сопоставление использует `fnmatch(3)`.

```
aggregate {
    # URL basename glob → directory crawl /spool/postgres-logs/pg/
    # с match=postgresql-2026-08-*.csv
    vrl file:///spool/postgres-logs/pg/postgresql-2026-08-*.csv name=postgresql_csv
        state=stream
        start_pattern='^\d{4}-\d{2}-\d{2}'
        condition_pattern='^\s'
        multiline_mode=continue_through;

    # Эквивалентная явная форма:
    # vrl file:///spool/postgres-logs/pg/ match=postgresql-2026-08-*.csv name=postgresql_csv;
}
```

Фильтруется только **basename** (без рекурсивных `**` / multi-directory glob). Новые файлы, появившиеся позже, подхватываются на следующем crawl tick (или через `notify=true`).


## Example of usage reading metrics from a file:
```
aggregate {
    prometheus_metrics file:///var/run/keepalived_time_state state=save notify=true;
}
```

## Period by default
В конфигурационном файле можно задать период по умолчанию для проверки ресурсов в context aggregate:
```
aggregate_period 10s;
```

* [service\_discovery](https://github.com/alligatormon/alligator/blob/master/doc/service-discovery.md) позволяет получать конфигурацию из экземпляров Consul или etcd.
