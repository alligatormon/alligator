## Unbound

To enable the collection of statistics from Unbound, use the following option:
### over unix socket
```
aggregate {
    unbound tls://unix:/var/run/unbound.sock tls_certificate=/etc/unbound/unbound_control.pem tls_key=/etc/unbound/unbound_control.key tls_ca=/etc/unbound/unbound_server.pem;
}
```

### over TLS socket
```
aggregate {
    unbound tls://localhost:8953 tls_certificate=/etc/unbound/unbound_control.pem tls_key=/etc/unbound/unbound_control.key tls_ca=/etc/unbound/unbound_server.pem;
}
```
