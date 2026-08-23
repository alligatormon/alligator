**Language / Язык:** [English](ntp.md) | [Русский](../ru/parsers/ntp.md)

## NTP

Sends an NTP client request over UDP and measures drift and RTT.

### Connection URL

```
udp://host[:123]
```

Default NTP port is **123**. Handler name is **`ntp`** (source file `ntpd.c`).

### Example

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

Unit tests: [`src/tests/unit2/parsers.h`](../../src/tests/unit2/parsers.h).
