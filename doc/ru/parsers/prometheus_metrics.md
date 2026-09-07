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

HashiCorp Vault (отдельного handler нет — тот же Prometheus parser, токен через `env=`):

```
aggregate {
    prometheus_metrics http://127.0.0.1:8200/v1/sys/metrics?format=prometheus
        env=X-Vault-Token:${VAULT_TOKEN};
}
```

Подробности: [vault.md](vault.md).

Из файла (опционально state / notify для filetailer):

```
aggregate {
    prometheus_metrics file:///var/run/keepalived_time_state state=save notify=true;
}
```

### Metrics

Имена метрик и labels берутся из текста exposition как у exporter (passthrough в хранилище Alligator).
