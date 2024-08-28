## Memcached

To enable the collection of statistics from memcached, use the following option:
### over TCP socket
```
aggregate {
    memcached tcp://localhost:11211;
}
```

### over TLS socket
```
aggregate {
    memcached tls://127.0.0.1:11211 tls_certificate=/etc/memcached/server-cert.pem tls_key=/etc/memcached/server-key.pem;
}
```

It is also useful to check process statistics, running services and open ports:
```
system {
    process memcached;
    services memcached.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="memcached", src_port="11211"})';
	make socket_match;
	datasource internal;
}

```
