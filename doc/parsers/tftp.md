## tftp

To enable the collection of statistics from tftp, use the following option:
```
aggregate {
    tftp udp://localhost:69/ping;
}
```
This will check the availability of the file `ping`.

It is also useful to check process statistics, running services, and open ports. TFTP is usually used with the inetd/xinetd service:
```
system {
    process xinetd;
    services xinetd.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="xinetd", src_port="69"})';
	make socket_match;
	datasource internal;
}
```
