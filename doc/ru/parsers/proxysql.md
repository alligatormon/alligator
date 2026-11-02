**Language / Язык:** [English](../../parsers/proxysql.md) | [Русский](proxysql.md)

# ProxySQL

Встроенный сбор метрик с admin-интерфейса ProxySQL (протокол MySQL). Имена метрик и label совместимы с [Percona proxysql_exporter](https://github.com/percona/proxysql_exporter).

Нужен модуль MySQL-клиента (как у `mysql` / `sphinxsearch`).

### URL подключения

```
mysql://[user:password@]host:6032/
```

Всегда указывайте admin-порт (`6032`). Если порт не задан, для `proxysql` alligator подставит `6032` (у обычного `mysql` по-прежнему `3306`).

Нужен пользователь с доступом к stats-таблицам (`stats` на ProxySQL ≥ 1.2.4). Для `runtime_mysql_servers` нужен `admin` (или аналог).

### Пример

```
modules {
	mysql /usr/lib64/mysql/libmysqlclient.so;
}

aggregate {
	proxysql mysql://stats:stats@127.0.0.1:6032/;
}
```

### Коллекторы по умолчанию (как в exporter)

| Источник | Метрики |
|----------|---------|
| `stats_mysql_global` | `proxysql_mysql_status_*` |
| `stats_mysql_connection_pool` | `proxysql_connection_pool_*` (`hostgroup`, `endpoint`) |
| `stats_mysql_processlist` (по клиенту) | `proxysql_processlist_client_connection_list` (`client_host`) |
| `global_variables` (`admin-version`) | `proxysql_info{version=…}` (значение `0`) |
| результат scrape | `proxysql_up` |

`status` пула числовой: `ONLINE=1`, `SHUNNED=2`, `OFFLINE_SOFT=3`, `OFFLINE_HARD=4`, `SHUNNED_REPLICATION_LAG=5`. Колонки `Latency_ms` / `Latency_us` отдаются как `proxysql_connection_pool_latency_us` (всегда микросекунды).

Неизвестные колонки / переменные ProxySQL тоже экспортируются (untyped).

`proxysql_up` означает успешный TCP/auth (как в Go-экспортере). Если `stats_mysql_global` или `admin-version` не вернули строк, `alligator_parser_ok` станет `0`.

### Опциональные коллекторы

По умолчанию выключены (как в proxysql_exporter). Включение через `env=` в aggregate:

| Флаг | Источник | Метрики |
|------|----------|---------|
| `env=collect.detailed_processlist:1` (alias: `collect.detailed.stats_mysql_processlist`) | детальный processlist | `proxysql_processlist_detailed_client_connection_count` — высокая кардинальность |
| `env=collect.runtime_mysql_servers:1` | `runtime_mysql_servers` | `proxysql_runtime_servers_*` (нужен admin) |
| `env=collect.stats_memory_metrics:1` | `stats_memory_metrics` | `proxysql_stats_memory_*` |

```
aggregate {
	proxysql mysql://admin:admin@127.0.0.1:6032/ env=collect.runtime_mysql_servers:1 env=collect.stats_memory_metrics:1;
}
```

Гистограммы command counter из Go-экспортера не реализованы.

Для произвольного SQL к ProxySQL используйте общий парсер [`mysql`](mysql.md) с `query {}`.

Каждый scrape также отдаёт session I/O серии alligator (`alligator_session_*`, `alligator_parser_ok`) с обычными label TCP-агрегатора.
