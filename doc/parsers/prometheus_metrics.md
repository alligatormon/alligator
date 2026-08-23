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

From a file (optional state / notify for filetailer):

```
aggregate {
    prometheus_metrics file:///var/run/keepalived_time_state state=save notify=true;
}
```

### Metrics

Metric names and labels are taken from the exposition text as written by the exporter (passthrough into the Alligator metric store).
