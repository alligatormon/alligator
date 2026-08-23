**Language / Язык:** [English](../../parsers/nats.md) | [Русский](nats.md)

## nats.io

Чтобы включить сбор статистики с NATS, используйте следующую опцию:
```
aggregate {
    nats http://localhost:4222;
}
```

### Сбор статистики
NATS должен быть запущен с опцией `-m 8222`.

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
```
system {
    process nats-server;
    services nats-server.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="nats-server", src_port="4222"})';
	make socket_match;
	datasource internal;
}
```
