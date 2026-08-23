**Language / Язык:** [English](../../parsers/powerdns.md) | [Русский](powerdns.md)

## PowerDNS

Чтобы включить сбор статистики с PowerDNS, используйте следующую опцию:
```
aggregate {
    powerdns http://localhost:8081 header=X-API-Key:test;
}
```

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
```
system {
    process pdns_server;
    services pdns.service;
}

}
query {
	expr 'count by (src_port, process) (socket_stat{process="pdns_server", src_port="8081"})';
	make socket_match;
	datasource internal;
}
query {
	expr 'count by (src_port, process) (socket_stat{process="pdns_server", src_port="53", proto="tcp"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="pdns_server", src_port="53", proto="udp"})';
	make socket_match;
	datasource internal;
}
```
