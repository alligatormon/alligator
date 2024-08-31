## nats.io

To enable the collection of statistics from NATS, use the following option:
```
aggregate {
    nats http://localhost:4222;
}
```

### To collect statistics
NATS should be runned with `-m 8222` option.

It is also useful to check process statistics, running services and open ports:
```
system {
    process nats-server;
    services nats-server.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="nats-server", src_port="4222"})';
	make socket_match;
	datasource internal;
}
```
