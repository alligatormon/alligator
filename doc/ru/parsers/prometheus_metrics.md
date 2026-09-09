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

OpenClaw Gateway (нативный Prometheus, bearer через `env=`; не открывайте публичный `/metrics`):

```
aggregate {
    prometheus_metrics http://127.0.0.1:18789/api/diagnostics/prometheus
        'env=Authorization:Bearer ${OPENCLAW_GATEWAY_TOKEN}';
}
```

Отдавайте scrape через `entrypoint` Alligator, чтобы scraper'ы не хранили Gateway operator token. `auth` на `entrypoint` опционален (без `auth bearer` порт открыт для scrape; при необходимости добавьте `auth` / TLS / `allow`/`deny`). Метрики сессий/cron/workspace с диска — отдельный handler [`openclaw`](openclaw.md).

JMX брокера Kafka через sidecar [jmx_exporter](https://github.com/prometheus/jmx_exporter) (метрики JVM/request; offset партиций и lag consumer — парсер `kafka`, [kafka.md](kafka.md)):

```
aggregate {
    kafka kafka://127.0.0.1:9092;
    prometheus_metrics http://127.0.0.1:5556/metrics;
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
