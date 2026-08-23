**Language / Язык:** [English](../../parsers/memcached.md) | [Русский](memcached.md)

## Memcached

Чтобы включить сбор статистики с memcached, используйте следующую опцию:
### через TCP-сокет
```
aggregate {
    memcached tcp://localhost:11211;
}
```

### через TLS-сокет
```
aggregate {
    memcached tls://127.0.0.1:11211 tls_certificate=/etc/memcached/server-cert.pem tls_key=/etc/memcached/server-key.pem;
}
```

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
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

### Запросы к memcached

Alligator поддерживает запросы ключей в Memcached. Следующий пример демонстрирует генерацию метрик по ключам в Memcached:
```
aggregate {
    memcached tcp://127.0.0.1:11211 name=mc;
}

query {
    expr "get first_metric test_metric third_metric";
    datasource mc;
    make memcached_query;
}
```
