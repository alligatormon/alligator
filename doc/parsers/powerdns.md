## PowerDNS

To enable the collection of statistics from PowerDNS, use the following option:
```
aggregate {
    powerdns http://localhost:8081 header=X-API-Key:test;
}
```

It is also useful to check process statistics, running services and open ports:
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
