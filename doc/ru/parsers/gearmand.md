**Language / Язык:** [English](../../parsers/gearmand.md) | [Русский](gearmand.md)

## Gearmand

Чтобы включить сбор статистики с gearmand, используйте следующую опцию:
```
aggregate {
    gearmand tcp://localhost:4730;
}
```

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
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
