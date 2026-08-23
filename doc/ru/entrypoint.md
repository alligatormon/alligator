**Language / Язык:** [English](../entrypoint.md) | [Русский](entrypoint.md)

# Entrypoint
Entrypoint нужен для включения портов, через которые Prometheus pull daemon'ы могут собирать информацию.
Конфигурация Alligator поддерживает список entrypoints, что даёт возможность иметь много разных портов для разных целей.

<br>
<p align="center">
<h1 align="center" style="border-bottom: none">
    <img alt="alligator-cluster-entrypoint" src="../images/entrypoint.jpeg"></a><br>
</h1>
<br>
<br>
</p>

Функциональность entrypoint включает:
- Push data interface
- Возврат метрик
- Alligator API
- Replication log для конфигурации [cluster](https://github.com/alligatormon/alligator/blob/master/doc/cluster.md).

Полное описание
```
entrypoint {
    log_level <time>;
    log_channel <channel_name>;
    log_channel_raw <channel_name>;
    return [empty|on]>;
    reject <label name> <label key>;
    auth <basic|bearer|other> <user:password|token|secret>;
    auth_header <header_name>;
    header <name> <value>;
    ttl <time to live>;
    read_metric_interval <seconds>;
    tcp <port>;
    tcp <addr>:<port>;
    tls <port>;
    tls <addr>:<port>;
    udp <port>;
    udp <addr>:<port>;
    unixgram <path/to/socket>;
    unix <path/to/socket>;
    allow <CIDR>;
    deny <CIDR>;
    api [on|off];
    tls_certificate <path>;
    tls_key <path>;
    tls_ca <path>;
    tls_verify [on|off];
    tls_verify_client [off|optional|require];
    tls_crl <path>;
    tls_crl_scope [leaf|chain];
    tls_ocsp [on|off];
    tls_ocsp_responder <http://url>;
    tls_ocsp_proxy <http://proxy:port>;
    tls_ocsp_timeout <time>;
    tls_ocsp_cache_ttl <time>;
    tls_revocation_mode [soft|hard];
    tls_ocsp_fetch [background|inline];
    handler <handler>;
    pingloop <number>;
    metric_aggregation [off|count]; # for counting histograms and counter datatypes as aggregation gateway
    format [openmetrics|prometheus]; # metrics exposition format for prometheus handler (default: openmetrics)
    # Prometheus histograms: TYPE histogram on base name (ut_hist) or on components
    # (ut_hist_bucket / _sum / _count) are merged into one family on scrape/export
    cluster <cluster_name>;
    instance <instance_name>;
    threads <number of threads>;
    lang <lang>;
    mapping {
        template <template>;
        name <name>;
        label <label_name> <label_value>;
        buckets <buckets 1> <buckets 2> ... <buckets N>;
        le <le 1> <le 2> ... <le N>;
        quantiles <quantile 1> <quantile 2> ... <quantile N>;
        match [glob];
    }
}
```

## log\_level
По умолчанию: off\
Множественное: нет\
Задаёт уровень логирования для этого context. Единицы для этой опции описаны в [документе](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-log-levels)

```
entrypoint {
    log_level debug;
    tcp 1111;
}
```

## return
По умолчанию: on\
Множественное: нет\
Возможные значения:
- on
- empty

Включение или отключение body ответа на запросы. Полезно для портов Alligator, доступных из интернета, как решение для приёма метрик Pushgateway из JavaScript браузера.

## reject
По умолчанию: -\
Множественное: да

Включает фильтр, отбрасывающий метрики с label name и value (tags для StatsD), равными полю.
Например, следующая конфигурация отбрасывает все метрики с labels **http_response_code="404"**, **name="bot"**:

```
entrypoint {
    reject http_response_code 404;
    reject name bot;
    tcp 1111;
}
```

## auth
По умолчанию: -\
Множественное: да

Возможные значения:
- `basic <user>:<password>`
- `bearer <token>`
- `other <secret>`

Опция "auth" позволяет использовать методы HTTP-аутентификации для доступа к этому порту.

Поддерживаются три типа HTTP-аутентификации: Basic, Bearer и простой secret. Basic authentication включает использование [login/password](https://datatracker.ietf.org/doc/html/rfc7617) для HTTP-аутентификации, Bearer authentication — статически заданных [tokens](https://datatracker.ietf.org/doc/html/rfc6750).
Тип "other" — пользовательский метод Alligator, который разрешает только статически заданные secret'ы в конфигурационном файле и сравнивает переданный клиентом header для проверки.

Alligator отвечает HTTP status code 401, если данные аутентификации не указаны, и 403, если аутентификация не прошла.

Конфигурация позволяет комбинировать несколько методов в одном context. Например, следующая конфигурация включает аутентификацию через любой из следующих header:
```
entrypoint {
    auth basic root:qwerty;
    auth basic user:password;
    auth bearer ZV0aFdP2kx44WVWRSkFCQsDhKvAHuA6Hhw4Kzr6OhoGe13RKxyUjgZo7ODco34sq;
    auth bearer F89rMiV1h8YyVhMIk9rI1GLSxW3fquSHCjf1PuAReABa47ivUbjshmTVZkD5bpXs;
    auth other LUTTK4SrdH;
    auth other RiLJXWD3Xu;
    tcp 1111;
}
```

Примеры авторизации на Alligator:
```
$ curl -w '%{response_code}\n' -u nouser:nopassword http://localhost:1111
403
$ curl -w '%{response_code}\n' -u root:qwerty http://localhost:1111
200
$ curl -w '%{response_code}\n' -H "Authorization: Bearer ZV0aFdP2kx44WVWRSkFCQsDhKvAHuA6Hhw4Kzr6OhoGe13RKxyUjgZo7ODco34sq" http://localhost:1111
200
$
```

## auth_header
По умолчанию: Authorization\
Множественное: нет

Эта опция позволяет изменить имя authorization header на другое.

## header
По умолчанию: -\
Множественное: да

Опция header позволяет отвечать клиентам пользовательскими headers. Пользовательские headers работают только если авторизация пройдена или метод запроса — OPTIONS.

Пример использования:

```
entrypoint {
    header access-control-allow-headers 'Authorization';
    header access-control-allow-methods POST;
    header access-control-allow-origin *;
    tcp 1111;
}
```

## ttl
По умолчанию: 300\
Множественное: нет\
Возможные значения:
- 0
- {any_number}{any_unit}


Эта опция позволяет задать максимальное время жизни метрик, отправленных методами statsd, pushgateway или graphite.
TTL в alligator даёт возможность удалять устаревшие метрики, которые долго не обновлялись.

Нулевое значение отключает функциональность TTL. Это означает, что полученные метрики никогда не удаляются;

Следующая конфигурация демонстрирует использование TTL. Метрики, не обновлявшиеся час, будут удалены.
```
entrypoint {
    ttl 1h;
    tcp 1111;
}
```

Чтобы отключить функциональность TTL:
```
entrypoint {
    ttl 0;
    tcp 1111;
}
```

Другой способ задать TTL для каждого push-запроса — header X-Expire-Time. Например, следующая команда создаёт метрику с TTL 1 day:
```
curl -sS -d "restore_test 1" -H "X-Expire-Time: 1d" 127.0.0.1:1111
```

Подробнее о единицах времени в конфигурации — в [документации](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md).

## read_metric_interval
По умолчанию: 10 (seconds)\
Множественное: нет

Минимальный wall-clock интервал между обновлениями `alligator_read_total` и `alligator_read_bytes_total` для stream entrypoints (TCP, TLS и Unix stream servers) при активных подключениях, и для UDP entrypoints **без** metric aggregation lock. Счётчики по-прежнему растут на каждом read или datagram; эта опция только ограничивает, как часто эти series записываются в metric tree. Опущенное или нулевое значение в JSON использует default 10 seconds. UDP listeners с lock по-прежнему сбрасывают через `aggregator_events_metric_add` на каждом datagram.

Plain configuration принимает целое число секунд. JSON entrypoints также принимают string или real value, разбираемые как другие duration fields (см. `mtail_full_export_interval`).


```
entrypoint {
    read_metric_interval 30;
    tcp 1111;
    udp 1111;
}
```


# tcp, tls, udp, unix, unixgram
По умолчанию: -\
Множественное: да

Конфигурация задаёт listen port (или socket) для входящих запросов. Это центральная часть context entrypoint. Любой entrypoint должен иметь хотя бы одну из этих опций для запуска entrypoint.
Alligator обычно слушает tcp port для ответа метриками Prometheus.
Конфигурация UDP обычно используется для метрик statsd, потому что udp — один из поддерживаемых протоколов statsd.
UDP и unix socket также можно использовать для получения stats rsyslog в [impstats](https://github.com/alligatormon/alligator/blob/master/doc/parsers/rsyslog.md).

Например, следующая конфигурация включает TCP- и UDP-порты для приёма метрик StatsD в режимах TCP и UDP:
```
entrypoint {
    tcp 1111;
    udp 1111;
}
```

В следующем примере alligator открывает два порта на INADDR_ANY и ещё один на адресе localhost:
```
entrypoint {
    tcp 1111 80 127.0.0.1:9000;
}
```

Дополнительный пример использования Unix-socket:
```
entrypoint {
        unix /tmp/alligator.sock;
}

system {
        base;
}
```

Для проверки используйте команду:
```
$ curl --unix-socket /tmp/alligator.sock localhost/metrics
context_switches_total 339451
cores_num 1
cores_num_cgroup 0
cores_num_hw 1
cpu_usage_calc_delta_seconds 18446744073.591579
cpu_usage_time {type="idle"} 2053.710000
cpu_usage_time {type="iowait"} 16.770000
cpu_usage_time {type="nice"} 0.000000
cpu_usage_time {type="system"} 21.850000
cpu_usage_time {type="user"} 83.900000
forks_total 4625
interrupts_total 135704
softirq 180882
time_now 1724141426
$
```

# allow, deny
По умолчанию: -\
Множественное: да\
Возможные значения:
- {IPv4 address}
- {IPv6 address}
- {IPv4 CIDR}
- {IPv6 CIDR}

Эти опции управляют политиками доступа для ограничений портов Alligator.
Важно: у этой опции есть политика по умолчанию. Если первый аргумент в конфигурации — "allow", политика по умолчанию будет "deny". И наоборот, если первый аргумент — "deny", доступ разрешён всем, кто не в списках "deny".

Следующая конфигурация — пример: доступ к порту 80 только с localhost, доступ к порту 1111 из частных сетей и более сложная политика доступа для порта 1112:
```
entrypoint {
    tcp 80;
    allow 127.0.0.1;
}

entrypoint {
    tcp 1111;
    allow 192.168.0.0/24;
    allow 172.16.0.0/12;
    allow 10.0.0.0/8;
}

entrypoint {
    tcp 1112;
    deny 100.64.0.0/10;
    deny 10.15.20.30;
    deny 127.0.0.2;
    allow 10.0.0.0/8;
}
```

## api
По умолчанию: off\
Множественное: нет\
Возможные значения:
- on
- off

Включает или отключает возможность делать PUT/POST запросы к Alligator API для обновления runtime-конфигурации. Документация API доступна в [документе](https://github.com/alligatormon/alligator/blob/master/doc/api.md)


## tls\_certificate
По умолчанию: -\
Множественное: нет

Задаёт путь к server x509 certificate.

## tls\_key
По умолчанию: -\
Множественное: нет

Задаёт путь к server x509 key.

## tls\_ca
По умолчанию: -\
Множественное: нет

CA bundle для проверки client certificates (mTLS) и валидации OCSP responses.

## tls\_verify / tls\_verify\_client
`tls_verify on` включает peer verification. `tls_verify_client require` дополнительно задаёт `SSL_VERIFY_FAIL_IF_NO_PEER_CERT`. `optional` запрашивает client cert без обязательного наличия.

## tls\_crl / tls\_ocsp
Те же ключи revocation, что у aggregate (`tls_crl`, `tls_crl_scope`, `tls_ocsp`, `tls_ocsp_responder`, `tls_ocsp_proxy`, `tls_ocsp_timeout`, `tls_ocsp_cache_ttl`, `tls_revocation_mode`, `tls_ocsp_fetch`). Используются для mTLS client certificates.

## handler
По умолчанию: prometheus\
Множественное: нет\
Возможные значения:
- prometheus
- rsyslog-impstats
- auditd
- lang
- log
- grok
- mtail

Эта опция задаёт handler, обрабатывающий полученные сообщения.

- **prometheus** (default) — Prometheus pull, pushgateway, statsd и graphite push queries.
- **rsyslog-impstats** — метрики Rsyslog impstats. См. [rsyslog](https://github.com/alligatormon/alligator/blob/master/doc/parsers/rsyslog.md).
- **auditd** — строки Linux audit log в формате `key=value`. См. [auditd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/auditd.md).
- **lang** — пользовательская обработка через внешние lang modules. Требует опцию `lang`. См. [lang](https://github.com/alligatormon/alligator/blob/master/doc/lang.md).
- **log** — только доставка логов: без парсинга метрик. Входящие байты пересылаются через `log_channel_raw` (см. [configuration — raw stream passthrough](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#raw-stream-passthrough-log_channel_raw)). Сочетайте с `log_channel_raw` на file/socket transports.
- **grok** — парсит stream именованным [grok](https://github.com/alligatormon/alligator/blob/master/doc/grok.md) context. Требует опцию `grok`.
- **mtail** — парсит stream именованной [mtail](https://github.com/alligatormon/alligator/blob/master/doc/mtail/README.md) program. Требует опцию `mtail`.


## grok
По умолчанию: -\
Множественное: нет

Имя grok context для использования при `handler grok`. Grok contexts задаются в top-level блоке [grok](https://github.com/alligatormon/alligator/blob/master/doc/grok.md).


## mtail
По умолчанию: -\
Множественное: нет

Имя mtail program для использования при `handler mtail`. Programs задаются в top-level блоке [mtail](https://github.com/alligatormon/alligator/blob/master/doc/mtail/configuration.md).


## mtail_full_export_interval
По умолчанию: 60s\
Множественное: нет

Минимальный интервал между полными export всех mtail variables при `handler mtail`. См. [mtail configuration](https://github.com/alligatormon/alligator/blob/master/doc/mtail/configuration.md#mtail_full_export_interval).


## namespace
По умолчанию: -\
Множественное: нет

Назначает метрики этого entrypoint в именованное пространство имён. См. [namespace](https://github.com/alligatormon/alligator/blob/master/doc/namespace.md).


## http_keepalive
По умолчанию: on (JSON entrypoints)\
Множественное: нет

Включает HTTP keep-alive для stream entrypoints, говорящих HTTP.


## http_idle_timeout
По умолчанию: 75s\
Множественное: нет

Idle timeout для HTTP keep-alive подключений на stream entrypoints.


## lang
По умолчанию: -\
Множественное: нет

Опция [lang](https://github.com/alligatormon/alligator/blob/master/doc/lang.md) задаёт context, обрабатывающий полученный body пользовательскими функциями через внешние модули.


## pingloop
По умолчанию: 0\
Множественное: нет\
Возможные значения:
- {number}

Pingloop позволяет blackbox handler'у пинговать ресурс более одного раза.


## metric_aggregation
По умолчанию: off\
Множественное: нет\
Возможные значения:
- off
- count

Эта опция позволяет считать counter и histogram метрики внутри Alligator вместо замены при push метрики протоколом pushgateway. Открывает интерфейс prom-aggregation-gateway. Graphite и statsd не могут задать это, так как уже поддерживают подсчёт метрик внутри протокола.


## format
По умолчанию: openmetrics\
Множественное: нет\
Возможные значения:
- openmetrics
- prometheus

Выбирает text exposition format, возвращаемый **prometheus** handler на GET scrape requests. По умолчанию Alligator отдаёт [OpenMetrics](https://openmetrics.io/) text (`Content-Type: application/openmetrics-text`) с завершающей строкой `# EOF`. Задайте `format prometheus` для классического Prometheus text format (`Content-Type: text/plain`).

Query parameters по-прежнему переопределяют эту настройку для отдельных запросов: `?format=prometheus`, `?format=openmetrics`, `?openmetrics=0` или `?openmetrics=1`.

Пример для Prometheus-compatible scrapers, ожидающих legacy format:

```
entrypoint {
    format prometheus;
    tcp 9100;
}
```


## cluster
По умолчанию: -\
Множественное: нет

Задаёт имя cluster для приёма метрик. Подробнее о cluster — в документации [cluster](https://github.com/alligatormon/alligator/blob/master/doc/cluster.md).

## instance
По умолчанию: -\
Множественное: нет\

Задаёт имя текущего instance cluster для приёма метрик. Подробнее о cluster — в документации [cluster](https://github.com/alligatormon/alligator/blob/master/doc/cluster.md).


## threads
По умолчанию: 0\
Множественное: нет\

Задаёт число потоков, работающих в этом entrypoint.

## add_labels
По умолчанию: -\
Множественное: да

Используйте `add_label <name>:<value>;` внутри `entrypoint`, чтобы добавить export labels ко всем метрикам с этого endpoint (например, `add_label exported_application:myapp;`).
Несколько директив `add_label` сливаются в один объект в JSON config output.

Следующая конфигурация — пример добавления labels для метрик, полученных на порту 80:
```
entrypoint {
    tcp 80;
    allow 127.0.0.1;
    add_label received:statsd;
    add_label server:alligator;
}
```

## metricstransform
По умолчанию: -\
Множественное: нет

`metricstransform` переписывает **ключи и/или значения labels** при ingest метрик, принятых этим entrypoint (например, pushgateway/statsd/graphite handlers). Преобразованные labels сохраняются в Alligator как переименованные или переписанные.

Значение — OTel-collector-like JSON object (или array) с правилами `transforms`, `operations` и `value_actions`. JSON может также использовать `new_label` или `label_key_actions` на каждой operation для переименования ключей (см. [action.md § metricstransform](https://github.com/alligatormon/alligator/blob/master/doc/action.md#metricstransform)); native plain block поддерживает `new_label`, но не `label_key_actions` (для regex-based key edits используйте JSON).

В plain config укажите либо **JSON string**, либо **native block** (см. [action.md § metricstransform](https://github.com/alligatormon/alligator/blob/master/doc/action.md#metricstransform) для keyword grammar; здесь то же самое). Native block может следовать за другими keywords на той же конструкции без точки с запятой перед `metricstransform` (те же chaining rules, что у [actions](https://github.com/alligatormon/alligator/blob/master/doc/action.md#metricstransform) и [aggregates](https://github.com/alligatormon/alligator/blob/master/doc/aggregate.md#metricstransform)).

Пример с JSON string (переписать `instance` в host-only value):
```
entrypoint {
    tcp 1111;
    handler prometheus;
    metricstransform '{"transforms":[{"include":"^.*$","match_type":"regexp","operations":[{"action":"update_label","label":"instance","value_actions":[{"regex":"^([^:]+):?.*$","replacement":"$1"}]}]}]}';
}
```

То же правило как native block:
```
entrypoint {
    tcp 1111;
    handler prometheus;
    metricstransform {
        include ^.*$ match_type regexp label instance regex '^([^:]+):?.*$' replacement '$1';
    };
}
```

Применяется в момент добавления метрики, поэтому преобразованные labels сразу сохраняются в Alligator.

Metric-name matching для этих правил использует **только** имя на ingest и в storage. В отличие от [actions](https://github.com/alligatormon/alligator/blob/master/doc/action.md#matching-metric-names-include-metric-metric-regex) на export time, entrypoints не применяют `metric_name_transform`, поэтому нет отдельного «on-the-wire» имени для matching.

## mapping
По умолчанию: -\
Множественное: да

Mapping включает поддержку функций агрегации и relabeling для push-протоколов statsd и graphite.
Пока единственный поддерживаемый метод matching — 'glob'.

## template
Задаёт template.

## name
Задаёт новое имя метрики.

## label
Задаёт новые labels.

## match
Задаёт опции для matching и split метрики с template.

## bucket
Включает разделение входных метрик на несколько buckets.

## le
Включает разделение входных метрик на несколько LE buckets.

## quantile
Включает расчёт quantiles по значениям метрик.

Пример использования statsd mapping:
```
entrypoint {
        udp 127.0.0.1:8125;
        tcp 8125;
        mapping {
                template test1.*.test2.*;
                name "$1"_"$2";
                label label_name_"$1" "$2"_key;
                buckets 100 200 300;
                match glob;
        }
        mapping {
                template test2.*.test3.*;
                name "$1"_"$2";
                label label_name_"$1" "$2"_key;
                le 100 200 300;
                match glob;
        }
        mapping {
                template test3.*.test4.*;
                name "$1"_"$2";
                label label_name_"$1" "$2"_key;
                quantiles 0.999 0.95 0.9;
                match glob;
        }
}
```
