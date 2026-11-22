**Language / Язык:** [English](../../parsers/haproxy.md) | [Русский](haproxy.md)

## HAproxy

Собирает статистику HAProxy через admin socket (`show stat`, `show info`, pools, sessions).

### Connection URL

```
tcp://host[:port]
unix:///path/to/haproxy.sock
```

### Пример

По TCP:

```
aggregate {
    haproxy tcp://localhost:9999;
}
```

По unix socket:

```
aggregate {
    haproxy unix:///var/run/haproxy.sock;
}
```

### Metrics

Колонки CSV `show stat` экспортируются отдельными OpenMetrics-семействами (не одной `haproxy_stat{type=…}`), с лейблами `proxy` и `server`:

- counters: `haproxy_sessions_total`, `haproxy_bytes_in_total`, `haproxy_http_responses_total{code=…}`, …
- gauges: `haproxy_current_sessions`, `haproxy_weight`, `haproxy_server_up`, …
- тайминги из CSV (мс) конвертируются в секунды: `haproxy_*_time_average_seconds`, `haproxy_check_duration_seconds`

`show info` → `haproxy_<lowercase_key>` (например `haproxy_connrate`). Pools: `haproxy_pool_*` / `haproxy_pools_*`. `show sess`: `haproxy_sessions`.
