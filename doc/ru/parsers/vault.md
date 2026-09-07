**Language / Язык:** [English](../../parsers/vault.md) | [Русский](vault.md)

## HashiCorp Vault

Отдельного handler **`vault` нет**. Vault уже отдаёт Prometheus: включите telemetry и скрейпите exposition через [`prometheus_metrics`](prometheus_metrics.md).

Включите Prometheus metrics в Vault (`telemetry { prometheus_retention_time = "24h" disable_hostname = true }` или аналог). Обычный путь — `/v1/sys/metrics?format=prometheus`. Неаутентифицированный доступ часто запрещён; токен передаётся через `env=`.

### Пример

```
aggregate {
    prometheus_metrics http://127.0.0.1:8200/v1/sys/metrics?format=prometheus
        env=X-Vault-Token:${VAULT_TOKEN};
}
```

TLS:

```
aggregate {
    prometheus_metrics https://vault.example:8200/v1/sys/metrics?format=prometheus
        tls_ca=/etc/vault/ca.crt
        env=X-Vault-Token:s.xxxxx;
}
```

### Metrics

Имена и labels берутся из Prometheus-текста Vault (`vault_*`, Go runtime, storage backends, …). Alligator их не переименовывает.

Проверка:

```
curl -sS http://127.0.0.1:1111/ | grep '^vault_'
```
