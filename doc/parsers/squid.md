## Squid

To enable the collection of statistics from Squid, use the following option:
```
aggregate {
	squid http://localhost:3128;
}
```

It is also useful to check process statistics, running services and open ports:
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
