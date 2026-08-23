**Language / Язык:** [English](../../parsers/riak.md) | [Русский](riak.md)

## Riak

Чтобы включить сбор статистики с Riak, используйте следующую опцию:
```
aggregate {
    riak http://localhost:8098;
}
```

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
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
