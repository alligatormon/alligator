**Language / Язык:** [English](../../parsers/riak.md) | [Русский](riak.md)

## Riak

Собирает HTTP stats Riak (порт по умолчанию **8098**).

### Connection URL

```
http://[user:pass@]host[:8098]
```

### Пример

```
aggregate {
    riak http://localhost:8098;
}
```

### Metrics

Поля JSON `/stats` становятся метриками `riak_*`.

Опциональные проверки process / socket:

```
system {
    services riak.service;
}

query {
    expr 'count by (src_port, process) (socket_stat{src_port="8098"})';
    make socket_match;
    datasource internal;
}
```
