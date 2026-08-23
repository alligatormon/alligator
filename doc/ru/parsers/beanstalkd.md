**Language / Язык:** [English](../../parsers/beanstalkd.md) | [Русский](beanstalkd.md)

## Beanstalkd

Собирает статистику tube и сервера Beanstalkd по TCP (порт по умолчанию **11300**).

### Connection URL

```
tcp://host[:11300]
```

### Пример

```
aggregate {
    beanstalkd tcp://localhost:11300;
}
```

### Metrics

Счётчики сервера и tube становятся `beanstalkd_*`.

Опциональные проверки process / socket:

```
system {
    process beanstalkd;
    services beanstalkd.service;
}

query {
    expr 'count by (src_port, process) (socket_stat{process="beanstalkd", src_port="11300"})';
    make socket_match;
    datasource internal;
}
```
