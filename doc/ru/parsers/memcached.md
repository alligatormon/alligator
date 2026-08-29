**Language / Язык:** [English](../../parsers/memcached.md) | [Русский](memcached.md)

## Memcached

Чтобы включить сбор статистики с memcached, используйте следующую опцию:
### через TCP-сокет
```
aggregate {
    memcached tcp://localhost:11211;
}
```

### через TLS-сокет
```
aggregate {
    memcached tls://127.0.0.1:11211 tls_certificate=/etc/memcached/server-cert.pem tls_key=/etc/memcached/server-key.pem;
}
```

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
```
system {
    process memcached;
    services memcached.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="memcached", src_port="11211"})';
	make socket_match;
	datasource internal;
}

```

### Метрики

Поля `stats` маппятся в Prometheus-подобные семейства (ломающее переименование относительно старых плоских gauges `memcached_<stat>`):

- Counters: `memcached_commands_total{command,status}`, `memcached_read_bytes_total` / `memcached_written_bytes_total`, счётчики соединений/элементов/LRU, `memcached_rusage_seconds_total{type}` и т.д.
- Gauges: `memcached_current_connections`, `memcached_current_bytes`, `memcached_current_items`, `memcached_limit_bytes`, gauges hash/slab/read-buffer; `memcached_uptime_seconds` — **counter**, `memcached_time_seconds` — gauge.

`cmd_get` / `cmd_touch` не экспортируются (есть hit/miss). `cmd_set` идёт как `memcached_commands_total{command="set",status="hit"}` после вычитания CAS (как в prometheus/memcached_exporter). Неизвестные числовые STAT-ключи — fallback `memcached_<key>` gauge.

## Dashboard

Dashboard memcached для Grafana + Prometheus доступен по [ссылке](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-memcached.json)

### Запросы к memcached

Alligator поддерживает запросы ключей в Memcached. Следующий пример демонстрирует генерацию метрик по ключам в Memcached:
```
aggregate {
    memcached tcp://127.0.0.1:11211 name=mc;
}

query {
    expr "get first_metric test_metric third_metric";
    datasource mc;
    make memcached_query;
}
```
