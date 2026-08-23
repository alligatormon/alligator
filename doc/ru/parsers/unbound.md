**Language / Язык:** [English](../../parsers/unbound.md) | [Русский](unbound.md)

## Unbound

Читает статистику Unbound с remote-control interface (TLS или unix).

### Connection URL

```
tls://unix:/path/to/unbound.sock
tls://host:8953
```

Нужны клиентские сертификаты control (`tls_certificate`, `tls_key`, `tls_ca`).

### Пример

По unix socket:

```
aggregate {
    unbound tls://unix:/var/run/unbound.sock
        tls_certificate=/etc/unbound/unbound_control.pem
        tls_key=/etc/unbound/unbound_control.key
        tls_ca=/etc/unbound/unbound_server.pem;
}
```

По TLS TCP (порт control по умолчанию **8953**):

```
aggregate {
    unbound tls://localhost:8953
        tls_certificate=/etc/unbound/unbound_control.pem
        tls_key=/etc/unbound/unbound_control.key
        tls_ca=/etc/unbound/unbound_server.pem;
}
```

### Metrics

Примеры: `unbound_uptime`, `unbound_cache_count{cache=…}`, `unbound_num_rrset_bogus`, `unbound_unwanted_{queries,replies}`, `unbound_recursion_time_avg`, `unbound_duration_microseconds`.
