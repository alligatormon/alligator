## Gearmand

To enable the collection of statistics from gearmand, use the following option:
```
aggregate {
    gearmand tcp://localhost:4730;
}
```

It is also useful to check process statistics, running services and open ports:
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
