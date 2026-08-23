**Language / Язык:** [English](mogilefs.md) | [Русский](../ru/parsers/mogilefs.md)

## MogileFS

Queries the MogileFS tracker over its text protocol.

### Connection URL

```
tcp://host[:7001]
```

Default tracker port is **7001**.

### Example

```
aggregate {
    mogilefs tcp://localhost:7001;
}
```

### Protocol

The parser sends tracker commands: `fsck_status`, `rebalance_status`, `get_hosts`, `get_devices`, `!stats`, `!jobs`, `!queue`.

### Metrics

Prefixes include:

- `mogilefs_fsck_*`, `mogilefs_rebalance_*`
- `mogilefs_host_list_*`, `mogilefs_device_list_*`
- `mogilefs_stats_*`, `mogilefs_jobs_*`, `mogilefs_queue_*`
