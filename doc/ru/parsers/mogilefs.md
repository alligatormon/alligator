**Language / Язык:** [English](../../parsers/mogilefs.md) | [Русский](mogilefs.md)

## MogileFS

Опрашивает MogileFS tracker по текстовому протоколу.

### Connection URL

```
tcp://host[:7001]
```

Порт tracker по умолчанию — **7001**.

### Пример

```
aggregate {
    mogilefs tcp://localhost:7001;
}
```

### Protocol

Parser отправляет команды: `fsck_status`, `rebalance_status`, `get_hosts`, `get_devices`, `!stats`, `!jobs`, `!queue`.

### Metrics

Префиксы:

- `mogilefs_fsck_*`, `mogilefs_rebalance_*`
- `mogilefs_host_list_*`, `mogilefs_device_list_*`
- `mogilefs_stats_*`, `mogilefs_jobs_*`, `mogilefs_queue_*`
