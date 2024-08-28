## Beanstalkd

To enable the collection of statistics from beanstalkd, use the following option:
```
aggregate {
    beanstalkd tcp://localhost:11300;
}
```

It is also useful to check process statistics, running services and open ports:
```
system {
    process beanstalkd;
    services beanstalkd.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="beanstalkd", src_port="11300"})';
	make socket_match;
	datasource internal;
}
