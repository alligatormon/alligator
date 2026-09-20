# PostgreSQL

PostgreSQL support by user queries:
```
aggregate {
	postgresql postgresql://postgres@localhost/postgres name=pg;
}

query {
	expr "SELECT pg_database.datname dbname, pg_database_size(pg_database.datname) as size FROM pg_database";
	field size;
	datasource pg;
	make postgresql_databases_size;
}
query {
	expr "SELECT count(datname) FROM pg_database";
	field count;
	datasource pg;
	make postgresql_databases_count;
}
query {
	expr "SELECT now() - pg_last_xact_replay_timestamp() AS replication_delay";
	field replication_delay;
	datasource pg;
	make postgresql_replication_lag;
}
```

`datasource pg/*` runs the query on every database listed from the instance. Use `except` on the query block to skip names (exact and `/regex/`). The same list also drops result rows whose `dbname` / `datname` / `psql_database` label matches, so it works with `datasource pg` queries that enumerate databases in SQL. Template and other system databases are not skipped unless listed in `except`. See [query except](../query.md).


# PgBouncer
```
aggregate {
	postgresql postgresql://pgbouncer:test@localhost:6432/pgbouncer;
}
```

# Odyssey
```
aggregate {
	odyssey postgresql://localhost:6432/console;
}
```

# pgpool
```

aggregate {
	pgpool postgresql://postgres@localhost:9999/postgres;
}
```

## `postgresql_error`

Connect and query failures increment `postgresql_error{name,reason}`. `reason` is a closed set of classified types, not the raw libpq or pooler text (session ids, routes, hosts, and IPs stay out of labels).

libpq wraps pooler/server FATAL with a prefix such as `connection to server at "host" (ip), port N failed:`. The classifier matches the inner product message first; that wrapper is not a type by itself. Bare TCP failures with no inner FATAL become `cannot_reach_server`.

Full `PQerrorMessage` / `PQresultErrorMessage` text stays in alligator error logs (`L_ERROR`).

Closed `reason` values:

- `route_not_found`, `routing_failed`, `tsa_host_not_found`, `all_backends_down`
- `backend_attach_failed`, `backend_connect_failed`, `backend_login_failed`
- `hba_rejected`, `auth_failed`, `user_blocked`
- `pool_size_reached`, `too_many_connections`
- `idle_in_transaction_timeout`, `idle_timeout`, `timeout`
- `database_missing`, `database_disabled`, `role_missing`, `role_cannot_login`, `permission_denied`
- `ssl_required`, `ssl_error`
- `system_not_ready`, `shutdown`, `soft_oom`, `replication_lag_rejected`, `protocol_error`
- `host_not_found`, `connection_refused`, `connection_reset`, `network_unreachable`, `server_closed`
- `cannot_reach_server`
- `unknown` (empty message), `other`
