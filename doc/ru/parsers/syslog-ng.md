**Language / Язык:** [English](../../parsers/syslog-ng.md) | [Русский](syslog-ng.md)

## syslog-ng

Читает внутреннюю статистику syslog-ng с control socket.

### Connection URL

```
unix:///var/lib/syslog-ng/syslog-ng.ctl
```

Путь соответствует control socket destination `stats()` в конфигурации syslog-ng.

### Пример

```
aggregate {
    syslog-ng unix:///var/lib/syslog-ng/syslog-ng.ctl;
}
```

См. [`src/tests/system/syslog-ng/alligator.conf`](../../../src/tests/system/syslog-ng/alligator.conf).

### Protocol

Alligator отправляет `STATS CSV\n` и разбирает CSV-строки.

### Metrics

- `syslogng_stats` с метками: `source_name`, `source_id`, `source_instance`, `state`, `type`

### Dashboard

Grafana dashboard: [`dashboards/alligator-syslog-ng.json`](../../../dashboards/alligator-syslog-ng.json)
