## PowerDNS

To enable the collection of statistics from PowerDNS, use the following option:
```
aggregate {
    powerdns http://localhost:8081 header=X-API-Key:test;
}
```

API `/api/v1/servers/localhost/statistics` fields become `powerdns_<name>` (hyphens → underscores). Cumulative stats are counters with a `_total` suffix (including byte counters such as `powerdns_udp_answers_bytes_total` / `powerdns_tcp_answers_bytes_total`). Size/memory/latency/uptime-style fields stay gauges (for example `powerdns_uptime`, `powerdns_packetcache_size`, `powerdns_packetcache_bytes`, `powerdns_xfr_queue`, `powerdns_latency`).

It is also useful to check process statistics, running services and open ports:
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
