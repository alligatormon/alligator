## Druid

To enable the collection of statistics from Druid, use the following option:
```
aggregate {
    druid http://localhost:8888;
    druid_worker http://localhost:8091;
    druid_historical http://localhost:8083;
    druid_broker http://localhost:8082;
}
```

It is also useful to check process statistics, running services and open ports:
```
system {
    process /druid/;
    services druid.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="java", src_port="8888"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="java", src_port="8091"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="java", src_port="8083"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="java", src_port="8082"})';
	make socket_match;
	datasource internal;
}

```

### Querying the Druid

Alligator supports querying in Clickhouse. The following example demonstrates generating metrics by SQL queries in Clickhouse:
```
aggregate {
    druid http://localhost:8123 name=druid;
}

query {
    expr "SELECT dt, app, user, metric FROM metrics";
    field metric;
    datasource druid;
    make druid_query;
}
```
