**Language / Язык:** [English](vault.md) | [Русский](../ru/parsers/vault.md)

## HashiCorp Vault

There is **no dedicated `vault` handler**. Vault already speaks Prometheus: enable telemetry and scrape the exposition with [`prometheus_metrics`](prometheus_metrics.md).

Turn on Prometheus metrics in Vault (`telemetry { prometheus_retention_time = "24h" disable_hostname = true }` or equivalent). The usual path is `/v1/sys/metrics?format=prometheus`. Unauthenticated access is often denied; pass a token with `env=`.

### Example

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

Names and labels come from Vault's Prometheus text (`vault_*`, Go runtime, storage backends, …). Alligator does not rename them.

Check:

```
curl -sS http://127.0.0.1:1111/ | grep '^vault_'
```
