**Language / Язык:** [English](../../parsers/druid.md) | [Русский](druid.md)

## Druid

Чтобы включить сбор статистики с Druid, используйте следующую опцию:
```
aggregate {
    druid http://localhost:8888;
    druid_worker http://localhost:8091;
    druid_historical http://localhost:8083;
    druid_broker http://localhost:8082;
}
```

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
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

### Запросы к Druid

Alligator поддерживает запросы к Clickhouse. Следующий пример демонстрирует генерацию метрик SQL-запросами в Clickhouse:
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
