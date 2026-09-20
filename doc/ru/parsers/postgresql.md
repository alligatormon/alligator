**Language / Язык:** [English](../../parsers/postgresql.md) | [Русский](postgresql.md)

# PostgreSQL

Поддержка PostgreSQL через пользовательские запросы:
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

`datasource pg/*` выполняет query на каждой базе экземпляра. `except` в блоке query пропускает имена (точные и `/regex/`). Тот же список отбрасывает строки результата с label `dbname` / `datname` / `psql_database`, поэтому он работает и с `datasource pg`, если SQL перечисляет базы в результате. Template и прочие системные базы не пропускаются, пока их нет в `except`. См. [query except](../query.md).


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

Сбои подключения и запросов увеличивают `postgresql_error{name,reason}`. `reason` — закрытый набор классифицированных типов, а не сырой текст libpq или пулера (идентификаторы сессий, маршруты, хосты и IP в label не попадают).

Каждая попытка подключения (успех или сбой) также обновляет общие session-метрики: gauge `alligator_session_connect_ok` (последняя попытка 1/0) и counter `alligator_session_connects_total`. Label совпадают с другими агрегаторами (`proto`, `type`, `host`, `parser`, `port`).

libpq оборачивает FATAL пулера/сервера префиксом вида `connection to server at "host" (ip), port N failed:`. Классификатор сначала ищет внутреннее сообщение продукта; сам префикс типом не является. Голый TCP-сбой без внутреннего FATAL даёт `cannot_reach_server`.

Полный текст `PQerrorMessage` / `PQresultErrorMessage` остаётся в логах alligator (`L_ERROR`).

Закрытый набор значений `reason`:

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
- `unknown` (пустое сообщение), `other`
