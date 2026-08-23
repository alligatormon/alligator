**Language / Язык:** [English](unbound.md) | [Русский](../ru/parsers/unbound.md)

## Unbound

Reads Unbound statistics from the remote-control interface (TLS or unix).

### Connection URL

```
tls://unix:/path/to/unbound.sock
tls://host:8953
```

Control client certificates are required (`tls_certificate`, `tls_key`, `tls_ca`).

### Example

Over unix socket:

```
aggregate {
    unbound tls://unix:/var/run/unbound.sock
        tls_certificate=/etc/unbound/unbound_control.pem
        tls_key=/etc/unbound/unbound_control.key
        tls_ca=/etc/unbound/unbound_server.pem;
}
```

Over TLS TCP (default control port **8953**):

```
aggregate {
    unbound tls://localhost:8953
        tls_certificate=/etc/unbound/unbound_control.pem
        tls_key=/etc/unbound/unbound_control.key
        tls_ca=/etc/unbound/unbound_server.pem;
}
```

### Metrics

Examples: `unbound_uptime`, `unbound_cache_count{cache=…}`, `unbound_num_rrset_bogus`, `unbound_unwanted_{queries,replies}`, `unbound_recursion_time_avg`, `unbound_duration_microseconds`.
