## Riak

To enable the collection of statistics from riak, use the following option:
```
aggregate {
    riak http://localhost:8098;
}
```

It is also useful to check process statistics, running services and open ports:
```
system {
    services riak.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{src_port="8098"})';
	make socket_match;
	datasource internal;
}
