**Language / Язык:** [English](../../parsers/squid.md) | [Русский](squid.md)

## Squid

Чтобы включить сбор статистики с Squid, используйте следующую опцию:
```
aggregate {
	squid http://localhost:3128;
}
```

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
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
