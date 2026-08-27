## Memcached

To enable the collection of statistics from memcached, use the following option:
### over TCP socket
```
aggregate {
    memcached tcp://localhost:11211;
}
```

### over TLS socket
```
aggregate {
    memcached tls://127.0.0.1:11211 tls_certificate=/etc/memcached/server-cert.pem tls_key=/etc/memcached/server-key.pem;
}
```

It is also useful to check process statistics, running services and open ports:
```
system {
    process memcached;
    services memcached.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="memcached", src_port="11211"})';
	make socket_match;
	datasource internal;
}

```

### Metrics

`stats` fields are mapped to Prometheus-style families (breaking rename vs older flat `memcached_<stat>` gauges):

- Counters: `memcached_commands_total{command,status}`, `memcached_read_bytes_total` / `memcached_written_bytes_total`, connection/item/LRU totals, `memcached_rusage_seconds_total{type}`, etc.
- Gauges: `memcached_current_connections`, `memcached_current_bytes`, `memcached_current_items`, `memcached_limit_bytes`, hash/slab/read-buffer gauges, `memcached_time_seconds`. `memcached_uptime_seconds` is a **counter**.

`cmd_get` / `cmd_touch` are not exported (covered by hit/miss breakdown). `cmd_set` is emitted as `memcached_commands_total{command="set",status="hit"}` after subtracting CAS counts (same semantics as prometheus/memcached_exporter). Unknown numeric STAT keys fall back to `memcached_<key>` gauges.

### Querying the memcached

Alligator supports querying keys in Memcached. The following example demonstrates generating metrics by keys in Memcached:
```
aggregate {
    memcached tcp://127.0.0.1:11211 name=mc;
}

query {
    expr "get first_metric test_metric third_metric";
    datasource mc;
    make memcached_query;
}
```
