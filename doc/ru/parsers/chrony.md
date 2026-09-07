**Language / Язык:** [English](../../parsers/chrony.md) | [Русский](chrony.md)

## Chrony

Отправляет datagram команды Chrony command-and-monitoring `REQ_TRACKING` и разбирает бинарный ответ (stratum, offsets, frequency, root delay/dispersion, update interval).

Имя handler — **`chrony`**. Агрегатор отправляет запрос как `mesg` по datagram-сокету.

Клиентский datagram биндится в тот же каталог, что и сокет Chrony (так требует `chronyd`). Alligator нужен write в этот каталог (обычно `root` или группа `chrony`).

### Connection URL

```
unixgram:///var/run/chrony/chronyd.sock
unixgram:///run/chrony/chronyd.sock
udp://127.0.0.1:323
```

### Пример

```
aggregate {
    chrony unixgram:///var/run/chrony/chronyd.sock;
}
```

### Metrics

- `chrony_stratum`
- `chrony_last_offset_seconds`, `chrony_rms_offset_seconds`
- `chrony_frequency_ppm`
- `chrony_skew_ppm`
- `chrony_root_delay_seconds`, `chrony_root_dispersion_seconds`
- `chrony_update_interval_seconds`

Unit tests: [`src/tests/unit2/parsers.h`](../../../src/tests/unit2/parsers.h) (`api_test_parser_chrony`).
