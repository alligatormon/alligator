# PostgreSQL

PostgreSQL support by user queries:
```
modules {
	postgresql /usr/lib64/libpq.so;
}

aggregate {
	postgresql postgresql://postgres@localhost name=pg;
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


# PgBouncer
```
modules {
	postgresql /usr/lib64/libpq.so;
}

aggregate {
	postgresql postgresql://pgbouncer:test@localhost:6432/pgbouncer;
}
```

# Odyssey
```
modules {
	postgresql /usr/lib64/libpq.so;
}

aggregate {
	odyssey postgresql://localhost:6432/console;
}
```

# pgpool
```
modules {
	postgresql /usr/lib64/libpq.so;
}

aggregate {
	pgpool postgresql://postgres@localhost:9999/postgres;
}
```
