**Language / Язык:** [English](../../parsers/unbound.md) | [Русский](unbound.md)

## Unbound

Чтобы включить сбор статистики с Unbound, используйте следующую опцию:
### через unix socket
```
aggregate {
    unbound tls://unix:/var/run/unbound.sock tls_certificate=/etc/unbound/unbound_control.pem tls_key=/etc/unbound/unbound_control.key tls_ca=/etc/unbound/unbound_server.pem;
}
```

### через TLS socket
```
aggregate {
    unbound tls://localhost:8953 tls_certificate=/etc/unbound/unbound_control.pem tls_key=/etc/unbound/unbound_control.key tls_ca=/etc/unbound/unbound_server.pem;
}
```
