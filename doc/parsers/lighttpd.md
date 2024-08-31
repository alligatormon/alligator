## Lighttpd

### To collect statistics
The Lighttpd provides [Mod\_status](https://redmine.lighttpd.net/projects/lighttpd/wiki/Mod_status) way to transmission of statistics.\
The options `status.status-url` and `status.statistics-url` should be specified in Lighttpd configuration.

To enable the collection of statistics from Lighttpd, use the following option:
```
aggregate {
    beanstalkd tcp://localhost:11300;
    process 'exec:///usr/sbin/lighttpd -tt -f /etc/lighttpd/lighttpd.conf';
}
```

The checking of `lighttpd -tt` generates the metric from exit status of lighttpd and made two metrics about configuration validation:
```
alligator_process_exit_status {proto="shell", key="exec:process:/usr/sbin/lighttpd -tt -f /etc/lighttpd/lighttpd.conf:/", type="aggregator"} 0
alligator_process_term_signal {proto="shell", key="exec:process:/usr/sbin/lighttpd -tt -f /etc/lighttpd/lighttpd.conf:/", type="aggregator"} 0
```

It is also useful to check process statistics, running services and open ports:
```
system {
    process lighttpd;
    services lighttpd.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="lighttpd", src_port="80"})';
	make socket_match;
	datasource internal;
}
