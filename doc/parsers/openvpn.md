**Language / Язык:** [English](openvpn.md) | [Русский](../ru/parsers/openvpn.md)

## OpenVPN

Parses OpenVPN **status dumps**: server status-version **1 / 2 / 3** files, client-mode `OpenVPN STATISTICS`, and the management-interface output of `status 2` (optional `load-stats`).

Handler name is **`openvpn`**.

OpenVPN writes the status file in place. Use **`state=forget`** on `file://` so each scrape re-reads the whole file from the beginning (default `stream` only tails appended bytes).

### Connection URL

```
file:///var/log/openvpn/status.log
tcp://127.0.0.1:7505
unix:///var/run/openvpn.sock
tcp://:management-password@127.0.0.1:7505
```

On TCP/unix/TLS the aggregator sends `load-stats`, `status 2`, then `quit`. If the URL has a password (`tcp://:secret@host:port` or `tcp://secret@host:port`), that line is sent first.

### Example

Status file (recommended):

```
aggregate {
    openvpn file:///var/log/openvpn/status.log state=forget;
}
```

Management interface:

```
aggregate {
    openvpn tcp://127.0.0.1:7505;
}
```

Enable a status file in OpenVPN:

```
status /var/log/openvpn/status.log
status-version 2
```

Or the management socket:

```
management 127.0.0.1 7505
management-client-user root
```

### Metrics

Server dump:

- `openvpn_connected_clients` — current client count
- `openvpn_status_updated_seconds` — UNIX time from the `TIME` line (v2/v3)
- `openvpn_client_received_bytes_total{common_name, real_address, virtual_address, username}`
- `openvpn_client_sent_bytes_total{common_name, real_address, virtual_address, username}`
- `openvpn_client_connected_since_seconds{common_name, real_address, virtual_address, username}` — UNIX time (v2/v3)
- `openvpn_route_last_ref_seconds{common_name, real_address, virtual_address}` — UNIX time (v2/v3)
- `openvpn_max_bcast_mcast_queue_length`
- `openvpn_bytes_in_total` / `openvpn_bytes_out_total` — from management `load-stats` when present

Client-mode statistics:

- `openvpn_bytes{kind="tun_tap_read|tun_tap_write|tcp_udp_read|tcp_udp_write|auth_read|pre_compress|post_compress|pre_decompress|post_decompress"}`

Unit tests: [`src/tests/unit2/parsers.h`](../../src/tests/unit2/parsers.h) (`api_test_parser_openvpn`).

It is still useful to watch the process and systemd unit:

```
system {
    process openvpn;
    services openvpn.service;
}
```
