**Language / Язык:** [English](../../parsers/openvpn.md) | [Русский](openvpn.md)

## OpenVPN

Разбирает **status dump** OpenVPN: серверные файлы status-version **1 / 2 / 3**, клиентский `OpenVPN STATISTICS` и вывод management-интерфейса `status 2` (опционально `load-stats`).

Имя handler — **`openvpn`**.

OpenVPN перезаписывает status-файл целиком. Для `file://` указывайте **`state=forget`**, чтобы каждый scrape читал файл с начала (режим `stream` по умолчанию только дочитывает добавленные байты).

### Connection URL

```
file:///var/log/openvpn/status.log
tcp://127.0.0.1:7505
unix:///var/run/openvpn.sock
tcp://:management-password@127.0.0.1:7505
```

На TCP/unix/TLS агрегатор отправляет `load-stats`, `status 2`, затем `quit`. Если в URL есть пароль (`tcp://:secret@host:port` или `tcp://secret@host:port`), эта строка уходит первой.

### Пример

Status-файл (рекомендуется):

```
aggregate {
    openvpn file:///var/log/openvpn/status.log state=forget;
}
```

Management-интерфейс:

```
aggregate {
    openvpn tcp://127.0.0.1:7505;
}
```

Включение status-файла в OpenVPN:

```
status /var/log/openvpn/status.log
status-version 2
```

Или management-сокет:

```
management 127.0.0.1 7505
management-client-user root
```

### Metrics

Серверный dump:

- `openvpn_connected_clients` — число клиентов
- `openvpn_status_updated_seconds` — UNIX-время из строки `TIME` (v2/v3)
- `openvpn_client_received_bytes_total{common_name, real_address, virtual_address, username}`
- `openvpn_client_sent_bytes_total{common_name, real_address, virtual_address, username}`
- `openvpn_client_connected_since_seconds{common_name, real_address, virtual_address, username}` — UNIX-время (v2/v3)
- `openvpn_route_last_ref_seconds{common_name, real_address, virtual_address}` — UNIX-время (v2/v3)
- `openvpn_max_bcast_mcast_queue_length`
- `openvpn_bytes_in_total` / `openvpn_bytes_out_total` — из management `load-stats`, если есть

Клиентская статистика:

- `openvpn_bytes{kind="tun_tap_read|tun_tap_write|tcp_udp_read|tcp_udp_write|auth_read|pre_compress|post_compress|pre_decompress|post_decompress"}`

Unit tests: [`src/tests/unit2/parsers.h`](../../../src/tests/unit2/parsers.h) (`api_test_parser_openvpn`).

Полезно также смотреть процесс и systemd unit:

```
system {
    process openvpn;
    services openvpn.service;
}
```
