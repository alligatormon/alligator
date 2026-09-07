**Language / Язык:** [English](chrony.md) | [Русский](../ru/parsers/chrony.md)

## Chrony

Sends a Chrony command-and-monitoring `REQ_TRACKING` datagram and parses the binary reply (stratum, offsets, frequency, root delay/dispersion, update interval).

Handler name is **`chrony`**. The aggregator sends the request as `mesg` over a datagram socket.

The client datagram is bound in the same directory as the Chrony socket (required by `chronyd`). Alligator needs write permission there (typically `root` or group `chrony`).

### Connection URL

```
unixgram:///var/run/chrony/chronyd.sock
unixgram:///run/chrony/chronyd.sock
udp://127.0.0.1:323
```

### Example

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

Unit tests: [`src/tests/unit2/parsers.h`](../../src/tests/unit2/parsers.h) (`api_test_parser_chrony`).
