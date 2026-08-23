**Language / Язык:** [English](syslog-ng.md) | [Русский](../ru/parsers/syslog-ng.md)

## syslog-ng

Reads syslog-ng internal statistics from the control socket.

### Connection URL

```
unix:///var/lib/syslog-ng/syslog-ng.ctl
```

Path matches the `stats()` destination control socket in syslog-ng configuration.

### Example

```
aggregate {
    syslog-ng unix:///var/lib/syslog-ng/syslog-ng.ctl;
}
```

See [`src/tests/system/syslog-ng/alligator.conf`](../../src/tests/system/syslog-ng/alligator.conf).

### Protocol

Alligator sends `STATS CSV\n` and parses CSV rows.

### Metrics

- `syslogng_stats` with labels: `source_name`, `source_id`, `source_instance`, `state`, `type`

### Dashboard

Grafana dashboard: [`dashboards/alligator-syslog-ng.json`](../../dashboards/alligator-syslog-ng.json)
