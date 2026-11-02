**Language / Язык:** [English](proxysql.md) | [Русский](../ru/parsers/proxysql.md)

# ProxySQL

Built-in scraper for the ProxySQL admin interface (MySQL protocol). Metric names and labels match [Percona proxysql_exporter](https://github.com/percona/proxysql_exporter).

Requires the MySQL client module (same as `mysql` / `sphinxsearch`).

### Connection URL

```
mysql://[user:password@]host:6032/
```

Always include the admin port (`6032`). If the port is omitted, alligator defaults ProxySQL scrapes to `6032` (generic `mysql` still defaults to `3306`).

Use a user that can read stats tables (`stats` on ProxySQL ≥ 1.2.4). `runtime_mysql_servers` needs `admin` (or equivalent).

### Example

```
modules {
	mysql /usr/lib64/mysql/libmysqlclient.so;
}

aggregate {
	proxysql mysql://stats:stats@127.0.0.1:6032/;
}
```

### Default collectors (exporter-compatible)

| Source | Metrics |
|--------|---------|
| `stats_mysql_global` | `proxysql_mysql_status_*` |
| `stats_mysql_connection_pool` | `proxysql_connection_pool_*` (`hostgroup`, `endpoint`) |
| `stats_mysql_processlist` (by client) | `proxysql_processlist_client_connection_list` (`client_host`) |
| `global_variables` (`admin-version`) | `proxysql_info{version=…}` (value `0`) |
| scrape result | `proxysql_up` |

Pool `status` is numeric: `ONLINE=1`, `SHUNNED=2`, `OFFLINE_SOFT=3`, `OFFLINE_HARD=4`, `SHUNNED_REPLICATION_LAG=5`. `Latency_ms` / `Latency_us` both export as `proxysql_connection_pool_latency_us` (value is always microseconds).

Unknown ProxySQL columns / variables are still exported (untyped).

`proxysql_up` means TCP/auth succeeded (same idea as the Go exporter). If `stats_mysql_global` or `admin-version` returns no rows, `alligator_parser_ok` is set to `0`.

### Optional collectors

Off by default (same as proxysql_exporter). Enable with `env=` on the aggregate line:

| Flag | Source | Metrics |
|------|--------|---------|
| `env=collect.detailed_processlist:1` (alias: `collect.detailed.stats_mysql_processlist`) | detailed processlist | `proxysql_processlist_detailed_client_connection_count` (`user`, `db`, `client_host`, `hostgroup`) — high cardinality |
| `env=collect.runtime_mysql_servers:1` | `runtime_mysql_servers` | `proxysql_runtime_servers_*` (needs admin) |
| `env=collect.stats_memory_metrics:1` | `stats_memory_metrics` | `proxysql_stats_memory_*` |

```
aggregate {
	proxysql mysql://admin:admin@127.0.0.1:6032/ env=collect.runtime_mysql_servers:1 env=collect.stats_memory_metrics:1;
}
```

Command-counter latency histograms from the Go exporter are not implemented.

For ad-hoc SQL on ProxySQL, use the generic [`mysql`](mysql.md) parser with `query {}` instead.

Each scrape also exports alligator session I/O series (`alligator_session_*`, `alligator_parser_ok`) with the usual TCP aggregator labels.
