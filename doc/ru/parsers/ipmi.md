**Language / Язык:** [English](../../parsers/ipmi.md) | [Русский](ipmi.md)

## IPMI

Запускает `ipmitool` через `exec://` и разбирает sensor, SEL, chassis, LAN и DCMI.

### Connection URL

```
exec:///path/to/ipmitool
```

Бинарник должен быть ipmitool (или совместимая обёртка). Alligator вызывает: `sensor`, `sel elist`, `chassis status`, `sel info`, `lan print`, `dcmi power reading`.

### Пример

```
aggregate {
    ipmi exec:///usr/bin/ipmitool;
}
```

См. [`src/tests/mock/ipmi/alligator.conf`](../../../src/tests/mock/ipmi/alligator.conf).

### Requirements

- Установленный и настроенный `ipmitool` (локальный BMC, LANplus и т.п.)
- Права на sensor и SEL команды

### Metrics

Примеры:

- `ipmi_sensor_{stat,status,thresholds}`
- `ipmi_eventlog_*`, `ipmi_lan`, `ipmi_dcmi_power_reading_*`
- Счётчики chassis и SEL
