## uWSGI 

### Describe
The uWSGI provides [different](https://uwsgi-docs.readthedocs.io/en/latest/StatsServer.html) ways to transmission of statistics.
It supports the TCP and unix-socket transport and plain and HTTP protocols.

### To collect statistics
Firstly, the option pm.status\_path = /stats should be specified in the php-fpm configuration.

To enable the collection of statistics from uWSGI, use the following option:
### over TCP socket
```
aggregate {
    uwsgi tcp://localhost:1717;
}
```

### over TCP socket with --stats-http option
```
aggregate {
    uwsgi http://localhost:1717;
}
```

### over Unix socket
```
aggregate {
    uwsgi unix:///tmp/uwsgi.sock;
}
```

### over Unix socket with --stats-http option
```
aggregate {
    uwsgi http://unix:/tmp/uwsgi.sock;
}
```


It is also useful to check process statistics, running services and open ports:
```
system {
    process uwsgi;
    services uwsgi.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="uwsgi", src_port="9090"})';
	make socket_match;
	datasource internal;
}
