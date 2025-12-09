## Clickhouse

To enable the collection of statistics from CH, use the following option:
### over HTTP socket
```
aggregate {
    clickhouse http://user:password@localhost:8123;
}
```

### over HTTPS socket
```
aggregate {
    clickhouse https://user:password@127.0.0.1:8443;
}
```
Note: the TCP port in clickhouse isn't supported by alligator.

It is also useful to check process statistics, running services and open ports:
```
system {
    process clickhouse-serv;
    services clickhouse-server.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="clickhouse-serv", src_port="8123"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="clickhouse-serv", src_port="8443"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="clickhouse-serv", src_port="9000"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="clickhouse-serv", src_port="9440"})';
	make socket_match;
	datasource internal;
}

```

### Querying the Clickhouse

Alligator supports querying in Clickhouse. The following example demonstrates generating metrics by SQL queries in Clickhouse:
```
aggregate {
    clickhouse http://user:password@localhost:8123 name=ch;
}

query {
    expr "SELECT dt, app, user, metric FROM metrics";
    field metric;
    datasource ch;
    make clickhouse_query;
}
```

### Pushing data to ClickHouse
Alligator also enables data to be pushed to ClickHouse using `action` methods.\
This feature provides the capability to set the engine for creating tables in ClickHouse. Alligator will make table for each metric and labels will passed as a columns.\
An example of usage is provided below. In this example, metrics will be pushed to the ClickHouse instance every 15 seconds:

```
scheduler {
    name sched-clickhouse;
    period 15s;
    datasource internal;
    action to-clickhouse;
}
action {
    name to-clickhouse;
    expr http://localhost:8123;
    serializer clickhouse;
    engine ENGINE=MergeTree ORDER BY timestamp;
}
```
```

## Dashboard
The system dashboard for Grafana + Prometheus is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-clickhouse.json)
<img alt="Dashboard" src="/doc/images/dashboard-clickhouse.jpg"><br>
