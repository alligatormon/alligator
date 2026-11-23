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

Gauge очередей и воркеров становятся `gearmand_*` (`gearmand_jobs` / `gearmand_server_jobs`, плюс running/available_workers).

Опциональные проверки process / socket:

```
system {
    process gearmand;
    services gearmand.service;
}

query {
    expr 'count by (src_port, process) (socket_listening{process="gearmand", src_port="4730"})';
    make socket_match;
    datasource internal;
}
```
