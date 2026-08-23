**Language / Язык:** [English](../../parsers/tftp.md) | [Русский](tftp.md)

## TFTP

Активная проверка TFTP: запрос файла по UDP и фиксация доступности / timing.

### Connection URL

```
udp://host[:69]/filename
```

Порт TFTP по умолчанию — **69**. Path — имя удалённого файла (например `/ping`).

### Пример

```
aggregate {
    tftp udp://localhost:69/ping;
}
```

Проверяет доступность файла `ping`.

### Metrics

Метрики связности в стиле blackbox для TFTP-передачи (success / timing).

Опциональные проверки process / socket (TFTP часто за inetd/xinetd):

```
system {
    process xinetd;
    services xinetd.service;
}

query {
    expr 'count by (src_port, process) (socket_stat{process="xinetd", src_port="69"})';
    make socket_match;
    datasource internal;
}
```
