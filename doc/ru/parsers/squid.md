**Language / Язык:** [English](../../parsers/squid.md) | [Русский](squid.md)

## Squid

Собирает статистику Squid cache manager по HTTP (порт по умолчанию **3128**).

### Connection URL

```
http://[user:pass@]host[:3128]
```

### Пример

```
aggregate {
    squid http://localhost:3128;
}
```

### Metrics

Поля cache manager становятся `squid_*`.

Опциональные проверки process / socket:

```
system {
    process squid;
    services squid.service;
}

query {
    expr 'count by (src_port, process) (socket_stat{process="squid", src_port="3128"})';
    make socket_match;
    datasource internal;
}
```
