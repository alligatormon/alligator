## Name Server Daemon (NSD)

To enable the collection of statistics from NSD, use the following option:
```
aggregate {
    zookeeper tcp://127.0.0.1:2181;
}
```

It is also useful to check process statistics, running services and open ports:
```
system {
    process /zookeeper/;
    services zookeeper.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="java", src_port="3888"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="java", src_port="2181"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="java", src_port="2888"})';
	make socket_match;
	datasource internal;
}
```
