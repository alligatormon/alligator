**Language / Язык:** [English](unbound.md) | [Русский](../ru/parsers/unbound.md)

## Unbound

Reads Unbound statistics from the remote-control interface (TLS or unix).

### Connection URL

```
tls://unix:/path/to/unbound.sock
tls://host:8953
```

Control client certificates are required (`tls_certificate`, `tls_key`, `tls_ca`).

### Example

Over unix socket:

```
aggregate {
    unbound tls://unix:/var/run/unbound.sock
        tls_certificate=/etc/unbound/unbound_control.pem
        tls_key=/etc/unbound/unbound_control.key
        tls_ca=/etc/unbound/unbound_server.pem;
}
```

Over TLS TCP (default control port **8953**):

```
aggregate {
    unbound tls://localhost:8953
        tls_certificate=/etc/unbound/unbound_control.pem
        tls_key=/etc/unbound/unbound_control.key
        tls_ca=/etc/unbound/unbound_server.pem;
}
```

### Metrics

Names follow [unbound_exporter](https://github.com/letsencrypt/unbound_exporter) where practical:

- Histogram (cumulative): `unbound_response_time_seconds_{bucket,sum,count}` with `le` in **seconds** (Unbound’s non-cumulative `histogram.*.to.*` buckets are converted; sum ≈ avg × count)
- Timing gauges: `unbound_recursion_time_seconds_{avg,median}`
- Counters: `unbound_queries_total{thread}`, `unbound_cache_hits_total{thread}`, `unbound_unwanted_queries_total`, `unbound_rrset_bogus_total`, `unbound_answer_rcodes_total{rcode}`, …
- Gauges: `unbound_cache_count{cache}`, `unbound_memory_caches_bytes{cache}`, `unbound_time_up_seconds_total`, `unbound_up`

## Dashboard

The unbound dashboard for Grafana + Prometheus is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-unbound.json)
