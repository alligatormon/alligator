**Language / Язык:** [English](patroni.md) | [Русский](../ru/parsers/patroni.md)

## Patroni

Collects Patroni HA PostgreSQL cluster status over HTTP.

### Connection URL

```
http://[user:pass@]host[:8008]
```

Default Patroni REST port is **8008**.

### Example

```
aggregate {
    patroni http://localhost:8008;
}
```

See [`src/tests/mock/patroni/alligator.conf`](../../src/tests/mock/patroni/alligator.conf).

### Endpoints

- `/patroni` — role and replication state
- `/cluster` — member lag and cluster view
- `/config` — settings (optional `pquery` for custom JSON paths)

### Metrics

Examples:

- `patroni_cluster_unlocked`, `patroni_xlog_location`, `patroni_replication_sync_priority`
- `patroni_role`, `patroni_state`
- `patroni_settings_*`
- `patroni_cluster_{members,receive_lag,replay_lag,lag}`
