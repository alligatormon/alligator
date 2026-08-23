**Language / Язык:** [English](../../parsers/prometheus_metrics.md) | [Русский](prometheus_metrics.md)

## OpenMetrics / Prometheus metrics

Собирает любой HTTP(S), file или другой transport, который отдаёт Prometheus text или OpenMetrics.

### Connection URL

Любой transport агрегатора с телом ответа, обычно:

```
http://host/metrics
https://host/metrics
file:///path/to/metrics.txt
```

### Пример

```
aggregate {
    prometheus_metrics http://localhost/metrics;
}
```

Из файла (опционально state / notify для filetailer):

```
aggregate {
    prometheus_metrics file:///var/run/keepalived_time_state state=save notify=true;
}
```

### Metrics

Имена метрик и labels берутся из текста exposition как у exporter (passthrough в хранилище Alligator).
