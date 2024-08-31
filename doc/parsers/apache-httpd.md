## Apache HTTP Server

### To collect statistics
The httpd provides [mod\_status](https://httpd.apache.org/docs/2.4/mod/mod_status.html) way to transmission of statistics.\
The next options should be specified in configuration file:
```
ExtendedStatus On
<Location /server-status>
    SetHandler server-status
</Location>
```

To enable the collection of statistics from httpd, use the following option:
```
aggregate {
    httpd http://localhost/server-status;
    process 'exec:///usr/sbin/apachectl configtest';
}
```

The checking of `apachectl configtest` generates the metric from exit status of httpd and made two metrics about configuration validation:
```
alligator_process_exit_status {proto="shell", key="exec:process:/usr/sbin/apachectl configtest:/", type="aggregator"} 0
alligator_process_term_signal {proto="shell", key="exec:process:/usr/sbin/apachectl configtest:/", type="aggregator"} 0
```

It is also useful to check process statistics, running services and open ports:
```
system {
    process httpd;
    services httpd.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="httpd", src_port="80"})';
	make socket_match;
	datasource internal;
}
```
