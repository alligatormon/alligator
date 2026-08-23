**Language / Язык:** [English](../../parsers/gearmand.md) | [Русский](gearmand.md)

## Gearmand

Собирает статус Gearman job server по TCP (порт по умолчанию **4730**).

### Connection URL

```
tcp://host[:4730]
```

### Пример

```
aggregate {
    gearmand tcp://localhost:4730;
}
```

### Metrics

Счётчики очередей и воркеров становятся `gearmand_*`.

Опциональные проверки process / socket:

```
system {
    process gearmand;
    services gearmand.service;
}

query {
    expr 'count by (src_port, process) (socket_stat{process="gearmand", src_port="4730"})';
    make socket_match;
    datasource internal;
}
```
