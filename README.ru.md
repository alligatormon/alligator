**Language / Язык:** [English](README.md) | [Русский](README.ru.md)

<h1 align="center" style="border-bottom: none">
    <img width="100" height="100" alt="Alligator" src="doc/images/logo.min.png"><br>Alligator
</h1>

<p align="center">
Alligator — агрегатор метрик системы и прикладного ПО. Универсальный инструмент для сбора и агрегации метрик из множества источников: приложений, ОС и других компонентов инфраструктуры. Позволяет комплексно мониторить и анализировать производительность и поведение серверов, получая видимость в инфраструктуру и приложения.
<br>
<br>
</p>


# Установка
Alligator поддерживает GNU/Linux и FreeBSD.
Инструкции по установке — в [distribution](doc/ru/distribution.md).

Больше примеров — в [тестах](https://github.com/alligatormon/alligator/tree/master/src/tests/system).

# Модульные тесты и покрытие
Набор `src/tests/unit2/` организован по заголовкам функциональности (например `netlib.h`, `http.h`, `parsers.h`) и держит тесты рядом с production-кодом.

Запуск покрытия (область: `src/**/*.c`, без `src/tests/**`, `src/external/**`, `src/build/**`):
```
cd src
./tests/coverage/run_coverage.sh
```

Артефакты:
- `src/tests/coverage/coverage_report.txt` — полный вывод `llvm-cov report`
- `src/tests/coverage/coverage_top15.txt` — 15 файлов с наименьшим покрытием
- `doc/coverage-baseline.md` — базовый снимок и пороги

Принципы тестирования — в `doc/testing.md` (English).

Минимальный порог локально:
```
cd src
MIN_LINE_COVERAGE=50 ./tests/coverage/run_coverage.sh
```

> Сборка тестов требует зависимостей проекта (например `jansson`) и инициализированных submodules.

# Командная строка

```
alligator [-h|--help] [-v|--version] [-l <level>] [<path>]
```

| Флаг | Описание |
|------|----------|
| `-h`, `--help` | Справка и выход |
| `-v`, `--version` | Версия и выход |
| `-l <num\|name>` | Уровень логирования (число или имя: `off`, `fatal`, `error`, `warn`, `info`, `debug`, `trace`) |
| `<path>` | Файл или каталог конфигурации (необязательно; можно указать несколько раз) |

Без аргументов используется basename `/etc/alligator` (Linux) или `/usr/local/etc/alligator` (FreeBSD).

# Описание конфигурации
Alligator поддерживает YAML, JSON и plain-text. В примерах ниже — plain text. Подробности — в [документации](doc/ru/configuration.md) и тестах.

Если путь не передан, alligator загружает конфигурацию по basename по умолчанию (`/etc/alligator` на Linux, `/usr/local/etc/alligator` на FreeBSD). Для basename проверяются по порядку:

1. `{basename}.json`
2. `{basename}.yaml`
3. `{basename}.conf`

Если `{basename}` — каталог, читаются все файлы с расширениями `.yaml`, `.json`, `.conf` (split / includes). Если `{basename}` — обычный файл, он парсится напрямую.

Явный путь на командной строке:
```
alligator /path/to/alligator.conf
alligator /etc/alligator.d/
```

Конфигурацию можно дополнять переменными окружения (`ALLIGATOR__…`). См. [configuration — переменные окружения](doc/ru/configuration.md#поддержка-переменных-окружения).

## Основная структура
Контексты для описания сбора данных:
- **aggregate**: сбор метрик из ПО через парсеры
- **query**: генерация новых метрик через PromQL или SQL-запросы к БД
- **namespace**: пространства имён метрик и лимиты выдачи (`max_emit`)
- **entrypoint**: приём метрик (Pushgateway, Statsd, Graphite); настройка портов для Prometheus и внутреннего API Alligator
- **lang**: вызов функций из подключаемых модулей
- **x509**: метрики из сертификатов PEM, JKS, PKCS
- **action**: запуск команд по поведению метрик или расписанию; экспорт в БД
- **scheduler**: периодический запуск `lang` и `action`
- **resolver**: DNS Alligator и метрики резолвинга
- **persistence**: сохранение метрик на диск между перезапусками
- **modules**: загрузка динамических библиотек (`.so`)
- **cluster**: кластеризация узлов
- **puppeteer**: сбор статистики загрузки HTTP-сайтов
- **chromecdp**: статистика загрузки в браузере через Chrome DevTools Protocol (без Node.js)
- **threaded_loop**: пулы потоков с event loop для отдельных задач
- **grok**: разбор логов в метрики (Grok, как в Elasticsearch)
- **mtail**: разбор логов скриптами, совместимыми с mtail (amtail)
- **vrl**: преобразование и обогащение событий логов (avrl)
- **enrichment_table**: таблицы CSV и MaxMind для VRL; см. [vrl](doc/ru/vrl/README.md)
- **probe**: модули blackbox-проверок для `GET /probe`; см. [probe](doc/ru/probe.md)

Подробная структура конфигурации — [configuration](doc/ru/configuration.md).


## Контекст entrypoint
См. [entrypoint](doc/ru/entrypoint.md).

Пример handler для Prometheus:
```
entrypoint {
    handler prometheus;
    tcp 1111;
}
```

## Сбор системных метрик
См. [system](doc/ru/system.md).

Пример сбора CPU, baseboard, ресурсов системы, памяти и сети:
```
system {
    base;
    disk;
    network;
}
```

## Контекст aggregate
Агрегатор собирает метрики из внешних источников и ПО по URL.

Периодические проверки, данные передаются в parser и превращаются в метрики.

Формат:
```
aggregate {
    <parser> <url> [<options>];
}
```

Пример: blackbox TCP/UDP/HTTP, файл, Redis:
```
aggregate_period 10s;
aggregate {
    # Blackbox checks
    blackbox tcp://google.com:80 add_label=url:google.com;
    blackbox tls://www.amazon.com:443 add_label=url:www.amazon.com;
    blackbox udp://8.8.8.8:53;
    blackbox http://yandex.ru;
    blackbox https://nova.rambler.ru/search 'env=User-agent:googlebot';
    prometheus_metrics file:///tmp/metrics-nostate.txt;
    blackbox file:///etc/ checksum=murmur3 file_stat=true calc_lines=true;
    redis tcp://localhost:6379/;
}
```

Подробнее — [aggregate](doc/ru/aggregate.md).


# Список парсеров ПО
Переведённые страницы — в `doc/ru/parsers/`.

- [rsyslog](doc/ru/parsers/rsyslog.md)
- [PostgreSQL](doc/ru/parsers/postgresql.md)
- [MongoDB](doc/ru/parsers/mongodb.md)
- [redis](doc/ru/parsers/redis.md)
- [clickhouse](doc/ru/parsers/clickhouse.md)
- [zookeeper](doc/ru/parsers/zookeeper.md)
- [memcached](doc/ru/parsers/memcached.md)
- [beanstalkd](doc/ru/parsers/beanstalkd.md)
- [gearmand](doc/ru/parsers/gearmand.md)
- [haproxy](doc/ru/parsers/haproxy.md)
- [blackbox](doc/ru/parsers/blackbox.md)
- [uwsgi](doc/ru/parsers/uwsgi.md)
- [nats](doc/ru/parsers/nats.md)
- [riak](doc/ru/parsers/riak.md)
- [rabbitmq](doc/ru/parsers/rabbitmq.md)
- [eventstore](doc/ru/parsers/eventstore.md)
- Celery [flower](doc/ru/parsers/flower.md)
- [powerdns](doc/ru/parsers/powerdns.md)
- [apache httpd](doc/ru/parsers/apache-httpd.md)
- [druid](doc/ru/parsers/druid.md)
- [couchbase](doc/ru/parsers/couchbase.md)
- [couchdb](doc/ru/parsers/couchdb.md)
- [mogilefs](doc/ru/parsers/mogilefs.md)
- [moosefs](doc/ru/parsers/moosefs.md)
- [kubernetes](doc/ru/parsers/kubernetes.md)
- [prometheus\_metrics](doc/ru/parsers/prometheus_metrics.md)
- [json\_query](doc/ru/parsers/json_query.md)
- [squid](doc/ru/parsers/squid.md)
- [bind](doc/ru/parsers/named.md) (nameD)
- [gdnsd](doc/ru/parsers/gdnsd.md)
- [tftp](doc/ru/parsers/tftp.md)
- [unbound](doc/ru/parsers/unbound.md)
- [syslog-ng](doc/ru/parsers/syslog-ng.md)
- [elasticsearch](doc/ru/parsers/elasticsearch.md)
- [opentsdb](doc/ru/parsers/opentsdb.md)
- [hadoop](doc/ru/parsers/hadoop.md)
- [snmp](doc/ru/parsers/snmp.md)
- [aerospike](doc/ru/parsers/aerospike.md)
- [lighttpd](doc/ru/parsers/lighttpd.md)
- [ipmi](doc/ru/parsers/ipmi.md)
- [keepalived](doc/ru/parsers/keepalived.md)
- [mysql](doc/ru/parsers/mysql.md)
- [monit](doc/ru/parsers/monit.md)
- [nginx](doc/ru/parsers/nginx.md)
- [nsd](doc/ru/parsers/nsd.md)
- [ntp](doc/ru/parsers/ntp.md)
- [nvidia-smi](doc/ru/parsers/nvidia-smi.md)
- [auditd](doc/ru/parsers/auditd.md)
- [cassandra](doc/ru/parsers/cassandra.md)
- [sentinel](doc/ru/parsers/sentinel.md)
- [patroni](doc/ru/parsers/patroni.md)
- [pgbouncer](doc/ru/parsers/postgresql.md#pgbouncer)
- [odyssey](doc/ru/parsers/postgresql.md#odyssey)
- [pgpool](doc/ru/parsers/postgresql.md#pgpool)
- [varnish](doc/ru/parsers/varnish.md)
- [wazuh](doc/ru/parsers/wazuh.md)


## Persistence
Каталог для сохранения метрик между перезапусками:
```
persistence {
    directory /var/lib/alligator;
}
```

## Modules
Контекст `modules` загружает `.so` в память:
```
modules {
	postgresql /usr/lib64/libpq.so;
	mysql /usr/lib/libmysqlclient.so;
}
```

Обычно используется в парсерах или контексте `lang`.

## Resolver
Гибкая настройка DNS, альтернативные серверы и метрики резолвинга. См. [resolver](doc/ru/resolver.md).

## Мониторинг сертификатов
См. [x509](doc/ru/x509.md).

Alligator проверяет срок действия сертификатов на файловой системе (`x509`) и в TLS-соединениях (`aggregate`, `entrypoint`). Опциональные проверки **CRL** и **OCSP** доступны для TLS aggregate, mTLS entrypoint и файловых коллекторов. Метрики: `x509_cert_expire_days`, `x509_cert_revocation_status`, `ocsp_requests_total`.

Пример конфигурации: [misc/examples/ocsp/alligator.conf](misc/examples/ocsp/alligator.conf).

## Queries
См. [query](doc/ru/query.md).

## Namespace
См. [namespace](doc/ru/namespace.md) и `max_emit`.

## Lang
[Lang](doc/ru/lang.md) загружает `.so` для сбора метрик (C/C++/Go/Rust).

## Actions
Запуск команд по расписанию или поведению метрик, экспорт в БД. См. [action](doc/ru/action.md).

## Scheduler
[scheduler](doc/ru/scheduler.md) — периодический запуск `lang` и `action`.

## Cluster
Синхронизация метрик между узлами. См. [cluster](doc/ru/cluster.md).

## Puppeteer
Сбор статистики загрузки HTTP-сайтов. См. [puppeteer](doc/ru/puppeteer.md).

## Chromecdp
Сбор статистики загрузки в Chrome/Chromium headless **без Node.js и npm-пакета Puppeteer**. Alligator запускает Chrome один раз, подключается по Chrome DevTools Protocol (CDP) через локальный WebSocket и обходит каждый URL в изолированном incognito-контексте на каждом цикле сбора. Метрики пишутся в хранилище alligator с именами в стиле Prometheus (`chromecdp_*`).

Требуется Chrome/Chromium с поддержкой CDP (например `chromium-browser` или `chromium-headless` на EL7/EL8).

```
chromecdp {
    executable /usr/bin/chromium-browser;
    port 9222;
    log_level off;
    concurrency 25;
    batch_size 2;
    batch_interval 1s;

    https://example.com {
        timeout        10s;
        ttl            120s;
        console_events true;
        add_label {
            team    sre;
            service web-check;
        }
        metricstransform {
            include ^chromecdp_.*$ match_type regexp label source regex '^https?://([^/]+).*$' replacement '$1';
        }
    }
}
```

Метрики: доступность страницы, HTTP-статусы ресурсов, длительность и размер загрузки, счётчики Chrome, Resource Timing API, опционально console/JS errors. Опции модуля: `concurrency`, `batch_size`, `batch_interval`, `setup_budget`, `post_nav_budget`. Опции URL совпадают с `puppeteer` где применимо. Цикл следует глобальному `aggregate_period`.

При `log_level off` (по умолчанию) подавляется stderr Chrome. `log_level info` и выше — для отладки.

Полная документация: [chromecdp.md](doc/ru/chromecdp.md). Сравнение с `puppeteer` — там же.

## Threaded loop
Пулы потоков с event loop. См. [threaded-loop](doc/ru/threaded-loop.md).

## Grok
Разбор логов в метрики по Grok-паттернам Elasticsearch. См. [grok](doc/ru/grok.md).

## Mtail
Разбор логов программами, совместимыми с mtail. См. [mtail](doc/ru/mtail/README.md).

## VRL
Преобразование событий логов программами avrl. См. [vrl](doc/ru/vrl/README.md).
