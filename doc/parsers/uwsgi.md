**Language / Язык:** [English](uwsgi.md) | [Русский](../ru/parsers/uwsgi.md)

## uWSGI

uWSGI exposes a [Stats Server](https://uwsgi-docs.readthedocs.io/en/latest/StatsServer.html) over TCP or Unix socket, plain or HTTP (`--stats-http`).

### Collect

#### TCP socket
```
aggregate {
    uwsgi tcp://localhost:1717;
}
```

#### TCP with `--stats-http`
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

#### Unix with `--stats-http`
```
aggregate {
    uwsgi http://unix:/tmp/uwsgi.sock;
}
```

### Metrics

| Metric | Type | Notes |
|--------|------|--------|
| `uwsgi_up` | gauge | `1` if the last stats scrape parsed |
| `uwsgi_listen_queue` / `uwsgi_signal_queue` / `uwsgi_load` | gauge | Master queue / load |
| `uwsgi_listen_queue_errors_total` | counter | Listen queue overflows |
| `uwsgi_locks_<name>` | gauge | Lock fields from stats |
| `uwsgi_sockets_<field>{name,proto}` | gauge | Per-socket integer fields |
| `uwsgi_workers` | gauge | Worker count |
| `uwsgi_worker_accepting` / `uwsgi_worker_idle` / `uwsgi_worker_busy` / `uwsgi_worker_in_request` | gauge | Aggregated worker state |
| `uwsgi_worker_requests_total` | counter | Sum of worker `requests` |
| `uwsgi_worker_exceptions_total` / `uwsgi_worker_harakiri_total` / `uwsgi_worker_signals_total` / `uwsgi_worker_respawns_total` | counter | Summed worker counters |
| `uwsgi_worker_transmit_bytes_total` | counter | Sum of worker `tx` |
| `uwsgi_worker_running_time_seconds_total` | counter | Sum of `running_time` (µs→seconds) |
| `uwsgi_worker_avg_response_time_seconds` | gauge | Mean of workers' `avg_rt` (µs→seconds) |
| `uwsgi_worker_static_requests_total` / `routed` / `offloaded` / `write_errors` / `read_errors` | counter | Summed core counters |
| `uwsgi_worker_delta_requests` / `uwsgi_worker_signal_queue` | gauge | Remaining aggregated gauges |

Worker fields are **summed** across workers (except `avg_rt`, which is a mean). Missing optional JSON keys no longer skip the rest of that worker.

## Dashboard

The uWSGI dashboard for Grafana + Prometheus is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-uwsgi.json)

It is also useful to check process statistics, running services and open ports:

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
