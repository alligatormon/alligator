# Query
The query context creates metrics based on a query. The query can run against local Alligator metric storage or an external database registered in `aggregate`.

## query_period
Default: server default\
Plural: no

Sets how often Alligator re-runs all `query` blocks. Use the same time units as elsewhere in configuration (`s`, `ms`, `h`, and so on).

```
query_period 20s;
```

This is separate from per-aggregate `period` (which controls polling of external targets).

## make
Specifies the name of the created metric for internal queries and serves as the key for API operations.


## expr
The query that is relevant to the specified database.\
For instance, it can be in promql format for internal Alligator metric storage. Alternatively, it can be an [SQL](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md) query.




Note: For `internal` datasource queries, `count()` uses PromQL syntax and returns the number of matched time series.
Unlike PromQL behavior, this implementation still returns a result even when no metrics match the filter.
Use `count by (<label>, ...) (...)` to keep grouping labels in the output instead of a single total value.

A bare metric identifier is an exact name. To match a family of names, use `{__name__=~"..."}` or `{__name__!~"..."}` (PCRE, unanchored). `name` is an alias of `__name__`. An invalid regex matches nothing; label `=~` on keys other than `__name__` is ignored.

```
query {
	expr 'count({__name__=~"^socket_stat"})';
	make socket_stat_series;
	datasource internal;
}
```


## action
Specifies the action context to run when expr is triggered. This is working only for 'internal' datasource.\
Here is an [explanation](https://github.com/alligatormon/alligator/blob/master/doc/action.md) of action context.


## datasource
Specifies the data source for the query.\
When making local metric requests, use the 'internal' key.\
For external databases, the 'name' field must be specified in the aggregator context with the database, and this name will serve as the datasource for the query.


## ns
When the database have internal namespaces (e.g., databases in a Relational DB), this field specifies the name of that namespace.


## field
Used to select the column in SQL responses. The column name will be used as the metric name and column values as the metric values.


## except
Skips databases by name when `datasource` is a wildcard (`pg/*`, `mysql/*`, and similar). Bare names are exact matches (hashtable). Tokens wrapped in `/…/` are PCRE (same convention as `system { process …; }`), unanchored unless the pattern includes `^`/`$`. Empty `except` skips nothing. Matching uses the database (or keyspace) name, not the full datasource key. System databases such as `template0` are not skipped unless listed.

If several query blocks share the same wildcard datasource with different `except` lists, a database is skipped only for the queries that match it. A per-database connection is skipped only when every query on that datasource excepts the database.

```
query {
    expr "
        WITH s AS (
            SELECT *
            FROM pg_stat_statements(false)
            WHERE calls > 0
              AND dbid = (SELECT oid FROM pg_database WHERE datname = current_database())
        ),
        topq AS (
            SELECT queryid
            FROM s
            ORDER BY total_exec_time DESC
            LIMIT 50
        )
        SELECT
            current_database() AS psql_database,
            s.queryid::text AS psql_queryid,
            round(sum(s.total_exec_time)::numeric, 3)::double precision AS postgresql_pgss_top_total_ms
        FROM s
        JOIN topq t USING (queryid)
        GROUP BY 1, 2;
    ";
    field postgresql_pgss_top_total_ms;
    datasource pg/*;
    except db1 db2 /db[0-9]/;
    make postgresql_pgss_top_total_ms;
}
```

JSON form: `"except": ["db1", "db2", "/db[0-9]/"]`.


## Examples

### Internal Alligator queries

This configuration will check the existance of process 'dockerd' on ports 8085 and 8080 and then create metric `socket_match` as a result of a query:
```
query_period 20s;
query {
	expr 'count by (src_port, process) (socket_stat{process="dockerd", src_port="8085"})';
	make socket_match;
	datasource internal;
}
query {
	expr 'count by (src_port, process) (socket_stat{process="dockerd", src_port="8080"})';
	make socket_match;
	datasource internal;
}
```

### External database queries

This configuration collect metrics about database sizes from an external MySQL instance:
```
aggregate {
	mysql mysql://user:password@127.0.0.1:3306 name=mysql;
}

query {
	expr 'SELECT table_schema "db_name", table_name "table", ROUND(SUM(data_length), 1) "mysql_table_size", ROUND(SUM(index_length), 1) "mysql_index_size", table_rows "mysql_table_rows" FROM information_schema.tables  GROUP BY table_schema';
	field mysql_table_size mysql_index_size mysql_table_rows;
	make mysql_db_size;
	datasource mysql;
}
```
