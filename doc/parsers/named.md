## Bind

```
statistics-channels {
    inet 127.0.0.1 port 8080 allow {any;};
};
```

### To collect statistics
Bind should be run with the specified `statistics-channels`:
```
statistics-channels {
    inet 127.0.0.1 port 8080 allow {any;};
};
```


Additionally, `zone-statistics` must be configured for each serving zone.
```
zone "localhost" IN {
        type master;
        file "named.localhost";
        allow-update { none; };
        zone-statistics yes;
};
```

To enable the collection of statistics from Bind, use the following option:
```
aggregate {
	named http://localhost:8080;
}
```

It is also useful to check process statistics, running services and open ports:
```
system {
    process named;
    services isc-bind-named;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="named", src_port="53", proto="tcp"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="named", src_port="53", proto="udp"})';
	make socket_match;
	datasource internal;
}
```
