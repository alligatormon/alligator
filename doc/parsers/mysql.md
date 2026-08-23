**Language / Язык:** [English](mysql.md) | [Русский](../ru/parsers/mysql.md)

# MySQL, MariaDB

Runs user SQL via the MySQL client protocol (`mysql://`) and turns selected columns into metrics through the `query` context. Also works with related SQL engines that speak MySQL (Manticore, Sphinx, ProxySQL) when the query fits.

### Connection URL

```
mysql://[user:password@]host[:3306]
```

Give the aggregate a `name=` so `query` can reference it as `datasource`.

### Example

```
aggregate {
    mysql mysql://user:password@localhost name=mysql;
}

query {
    expr "SELECT table_schema \"db_name\", ROUND(SUM(data_length + index_length), 1) \"mysql_database_size\" FROM information_schema.tables GROUP BY table_schema";
    field mysql_database_size;
    datasource mysql;
    make mysql_db_size;
}
```

### Metrics

Metric names come from the `make` / `field` settings in `query` (for example `mysql_db_size` / `mysql_database_size`). See [query.md](../query.md).
