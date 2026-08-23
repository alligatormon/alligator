**Language / Язык:** [English](../../parsers/clickhouse.md) | [Русский](clickhouse.md)

## Clickhouse

Чтобы включить сбор статистики с CH, используйте следующую опцию:
### через HTTP-сокет
```
aggregate {
    clickhouse http://user:password@localhost:8123;
}
```

### через HTTPS-сокет
```
aggregate {
    clickhouse https://user:password@127.0.0.1:8443;
}
```
Примечание: TCP-порт в clickhouse не поддерживается alligator.

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
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

### Запросы к Clickhouse

Alligator поддерживает запросы к Clickhouse. Следующий пример демонстрирует генерацию метрик SQL-запросами в Clickhouse:
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

### Отправка данных в ClickHouse
Alligator также позволяет отправлять данные в ClickHouse методами `action`.\
Эта возможность позволяет задать движок для создания таблиц в ClickHouse. Alligator создаст таблицу для каждой метрики, а метки будут переданы как столбцы.\
Ниже приведён пример использования. В этом примере метрики будут отправляться в экземпляр ClickHouse каждые 15 секунд:

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


## Dashboard
Системный dashboard для Grafana + Prometheus доступен по следующей [ссылке](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-clickhouse.json)
<img alt="Dashboard" src="../../images/dashboard-clickhouse.jpg"><br>
