**Language / Язык:** [English](haproxy.md) | [Русский](../ru/parsers/haproxy.md)

## HAproxy

Collects HAProxy stats over the admin socket (`show stat`, `show info`, pools, sessions).

### Connection URL

```
tcp://host[:port]
unix:///path/to/haproxy.sock
```

### Example

Over TCP:

```
aggregate {
    haproxy tcp://localhost:9999;
}
```

Over unix socket:

```
aggregate {
    haproxy unix:///var/run/haproxy.sock;
}
```

### Metrics

`show stat` CSV columns are exported as separate OpenMetrics families (not a single `haproxy_stat{type=…}` series), with labels `proxy` and `server`:

- counters: `haproxy_sessions_total`, `haproxy_bytes_in_total`, `haproxy_bytes_out_total`, `haproxy_http_responses_total{code=…}`, …
- gauges: `haproxy_current_sessions`, `haproxy_current_queue`, `haproxy_weight`, `haproxy_server_up`, …
- timings from CSV (ms) are converted to seconds: `haproxy_queue_time_average_seconds`, `haproxy_check_duration_seconds`, …

`show info` becomes `haproxy_<lowercase_key>` (e.g. `haproxy_connrate`). Pools: `haproxy_pool_*` / `haproxy_pools_*`. `show sess` count: `haproxy_sessions`.
