**Language / Язык:** [English](../../parsers/ntp.md) | [Русский](ntp.md)

## NTP

Отправляет NTP client request по UDP и измеряет drift и RTT.

### Connection URL

```
udp://host[:123]
```

Порт NTP по умолчанию — **123**. Имя handler — **`ntp`** (исходник `ntpd.c`).

### Пример

```
aggregate {
    ntp udp://time.google.com:123;
}
```

### Metrics

- `ntp_drift_seconds`, `ntp_stratum`, `ntp_leap`
- `ntp_root_dispersion_seconds`, `ntp_root_delay_seconds`
- `ntp_precision_miliseconds`, `ntp_reference_timestamp_seconds`
- `ntp_rtt_seconds`, `ntp_root_distance_seconds`

Unit tests: [`src/tests/unit2/parsers.h`](../../../src/tests/unit2/parsers.h).
