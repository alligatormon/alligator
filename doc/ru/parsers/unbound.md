**Language / Язык:** [English](../../parsers/unbound.md) | [Русский](unbound.md)

## Unbound

Читает статистику Unbound с remote-control interface (TLS или unix).

### Connection URL

```
tls://unix:/path/to/unbound.sock
tls://host:8953
```

Нужны клиентские сертификаты control (`tls_certificate`, `tls_key`, `tls_ca`).

### Пример

По unix socket:

```
aggregate {
    unbound tls://unix:/var/run/unbound.sock
        tls_certificate=/etc/unbound/unbound_control.pem
        tls_key=/etc/unbound/unbound_control.key
        tls_ca=/etc/unbound/unbound_server.pem;
}
```

По TLS TCP (порт control по умолчанию **8953**):

```
aggregate {
    unbound tls://localhost:8953
        tls_certificate=/etc/unbound/unbound_control.pem
        tls_key=/etc/unbound/unbound_control.key
        tls_ca=/etc/unbound/unbound_server.pem;
}
```

### Metrics

Имена близки к [unbound_exporter](https://github.com/letsencrypt/unbound_exporter):

- Histogram (кумулятивный): `unbound_response_time_seconds_{bucket,sum,count}` с `le` в **секундах** (некумулятивные `histogram.*.to.*` конвертируются; sum ≈ avg × count)
- Timing: `unbound_recursion_time_seconds_{avg,median}`
- Counters: `unbound_queries_total{thread}`, `unbound_cache_hits_total{thread}`, `unbound_unwanted_queries_total`, …
- Gauges: `unbound_cache_count{cache}`, `unbound_memory_caches_bytes{cache}`, `unbound_time_up_seconds_total`, `unbound_up`

## Dashboard

Dashboard unbound для Grafana + Prometheus доступен по следующей [ссылке](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-unbound.json)
