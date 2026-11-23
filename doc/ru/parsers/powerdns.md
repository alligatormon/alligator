**Language / Язык:** [English](../../parsers/powerdns.md) | [Русский](powerdns.md)

## PowerDNS

Чтобы включить сбор статистики с PowerDNS, используйте следующую опцию:
```
aggregate {
    powerdns http://localhost:8081 header=X-API-Key:test;
}
```

Поля API `/api/v1/servers/localhost/statistics` становятся `powerdns_<name>` (дефисы → `_`). Накопительные статистики — counters с суффиксом `_total` (включая байтовые счётчики вроде `powerdns_udp_answers_bytes_total` / `powerdns_tcp_answers_bytes_total`). Size/memory/latency/uptime остаются gauges (например `powerdns_uptime`, `powerdns_packetcache_size`, `powerdns_packetcache_bytes`, `powerdns_xfr_queue`, `powerdns_latency`).

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
```
system {
    process pdns_server;
    services pdns.service;
}

}
query {
	expr 'count by (src_port, process) (socket_listening{process="pdns_server", src_port="8081"})';
	make socket_match;
	datasource internal;
}
query {
	expr 'count by (src_port, process) (socket_listening{process="pdns_server", src_port="53", proto="tcp"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_listening{process="pdns_server", src_port="53", proto="udp"})';
	make socket_match;
	datasource internal;
}
```
