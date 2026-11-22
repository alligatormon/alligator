**Language / Язык:** [English](../../parsers/uwsgi.md) | [Русский](uwsgi.md)

## uWSGI

uWSGI отдаёт [Stats Server](https://uwsgi-docs.readthedocs.io/en/latest/StatsServer.html) по TCP или Unix-сокету, plain или HTTP (`--stats-http`).

### Сбор

#### TCP socket
```
aggregate {
    uwsgi tcp://localhost:1717;
}
```

#### TCP с `--stats-http`
```
aggregate {
    uwsgi http://localhost:1717;
}
```

#### Unix socket
```
aggregate {
    uwsgi unix:///tmp/uwsgi.sock;
}
```

#### Unix с `--stats-http`
```
aggregate {
    uwsgi http://unix:/tmp/uwsgi.sock;
}
```

### Метрики

| Метрика | Тип | Примечание |
|---------|-----|------------|
| `uwsgi_up` | gauge | `1`, если последний scrape распарсился |
| `uwsgi_listen_queue` / `uwsgi_signal_queue` / `uwsgi_load` | gauge | Очереди / load мастера |
| `uwsgi_listen_queue_errors_total` | counter | Overflow listen queue |
| `uwsgi_locks_<name>` | gauge | Поля locks из stats |
| `uwsgi_sockets_<field>{name,proto}` | gauge | Целочисленные поля сокета |
| `uwsgi_workers` | gauge | Число воркеров |
| `uwsgi_worker_accepting` / `uwsgi_worker_idle` / `uwsgi_worker_busy` / `uwsgi_worker_in_request` | gauge | Агрегированное состояние |
| `uwsgi_worker_requests_total` | counter | Сумма `requests` |
| `uwsgi_worker_exceptions_total` / `uwsgi_worker_harakiri_total` / `uwsgi_worker_signals_total` / `uwsgi_worker_respawns_total` | counter | Суммы счётчиков воркеров |
| `uwsgi_worker_transmit_bytes_total` | counter | Сумма `tx` |
| `uwsgi_worker_running_time_seconds_total` | counter | Сумма `running_time` (µs→секунды) |
| `uwsgi_worker_avg_response_time_seconds` | gauge | Среднее `avg_rt` воркеров (µs→секунды) |
| `uwsgi_worker_static_requests_total` / routed / offloaded / write_errors / read_errors | counter | Суммы core-счётчиков |
| `uwsgi_worker_delta_requests` / `uwsgi_worker_signal_queue` | gauge | Прочие агрегаты |

Поля воркеров **суммируются** (кроме `avg_rt` — среднее). Отсутствие опционального ключа больше не обрывает разбор воркера.

## Dashboard

Dashboard uWSGI для Grafana + Prometheus доступен по [ссылке](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-uwsgi.json)

Также полезно смотреть процесс, сервис и порты:

```
system {
    process uwsgi;
    services uwsgi.service;
}

query {
	expr 'count by (src_port, process) (socket_listening{process="uwsgi", src_port="9090"})';
	make socket_match;
	datasource internal;
}
```
