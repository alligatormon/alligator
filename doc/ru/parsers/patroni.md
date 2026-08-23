**Language / Язык:** [English](../../parsers/patroni.md) | [Русский](patroni.md)

## Patroni

Собирает статус HA PostgreSQL кластера Patroni по HTTP.

### Connection URL

```
http://[user:pass@]host[:8008]
```

Порт REST Patroni по умолчанию — **8008**.

### Пример

```
aggregate {
    patroni http://localhost:8008;
}
```

См. [`src/tests/mock/patroni/alligator.conf`](../../../src/tests/mock/patroni/alligator.conf).

### Endpoints

- `/patroni` — role и replication state
- `/cluster` — lag участников и вид кластера
- `/config` — settings (опциональный `pquery` для JSON paths)

### Metrics

Примеры:

- `patroni_cluster_unlocked`, `patroni_xlog_location`, `patroni_replication_sync_priority`
- `patroni_role`, `patroni_state`
- `patroni_settings_*`
- `patroni_cluster_{members,receive_lag,replay_lag,lag}`
