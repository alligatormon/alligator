**Language / Язык:** [English](prometheus_metrics.md) | [Русский](../ru/parsers/prometheus_metrics.md)

## OpenMetrics / Prometheus metrics

Scrapes any HTTP(S), file, or other transport that returns Prometheus text or OpenMetrics exposition format.

### Connection URL

Any aggregator transport with body content, typically:

```
http://host/metrics
https://host/metrics
file:///path/to/metrics.txt
```

### Example

```
aggregate {
    prometheus_metrics http://localhost/metrics;
}
```

HashiCorp Vault (no dedicated handler — same Prometheus parser, token via `env=`):

```
aggregate {
    prometheus_metrics http://127.0.0.1:8200/v1/sys/metrics?format=prometheus
        env=X-Vault-Token:${VAULT_TOKEN};
}
```

See [vault.md](vault.md) for TLS and Vault telemetry notes.

OpenClaw Gateway (native Prometheus, bearer via `env=`; prefer this over a public `/metrics`):

```
aggregate {
    prometheus_metrics http://127.0.0.1:18789/api/diagnostics/prometheus
        'env=Authorization:Bearer ${OPENCLAW_GATEWAY_TOKEN}';
}
```

Re-export from Alligator so scrapers never hold the Gateway operator token. `entrypoint` `auth` is optional (omit `auth bearer` for an open scrape port, or add it / TLS / `allow`/`deny` if you want Alligator protected). Filesystem session/cron/workspace metrics use the dedicated [`openclaw`](openclaw.md) handler.

Kafka broker JMX via [jmx_exporter](https://github.com/prometheus/jmx_exporter) sidecar (JVM/request metrics; partition offsets and consumer lag come from the `kafka` parser instead — [kafka.md](kafka.md)):

```
aggregate {
    kafka kafka://127.0.0.1:9092;
    prometheus_metrics http://127.0.0.1:5556/metrics;
}
```

From a file (optional state / notify for filetailer):

```
aggregate {
    prometheus_metrics file:///var/run/keepalived_time_state state=save notify=true;
}
```

### Metrics

Metric names and labels are taken from the exposition text as written by the exporter (passthrough into the Alligator metric store).
