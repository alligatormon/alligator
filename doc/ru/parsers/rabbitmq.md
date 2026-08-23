**Language / Язык:** [English](../../parsers/rabbitmq.md) | [Русский](rabbitmq.md)

## RabbitMQ

Чтобы включить сбор статистики с RabbitMQ, используйте следующую опцию:
```
aggregate {
    rabbitmq http://guest:guest@localhost:15672;
}
```

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
```
system {
    process beam.smp;
    services rabbitmq-server.service;
}

query {
    expr 'count by (src_port, process) (socket_stat{process="beam.smp", src_port="25672"})';
    make socket_match;
    datasource internal;
}
query {
    expr 'count by (src_port, process) (socket_stat{process="beam.smp", src_port="15692"})';
    make socket_match;
    datasource internal;
}
query {
    expr 'count by (src_port, process) (socket_stat{process="beam.smp", src_port="15672"})';
    make socket_match;
    datasource internal;
}
query {
    expr 'count by (src_port, process) (socket_stat{process="beam.smp", src_port="5672"})';
    make socket_match;
    datasource internal;
}
query {
    expr 'count by (src_port, process) (socket_stat{process="epmd", src_port="4369"})';
    make socket_match;
    datasource internal;
}
```

## Dashboard
Системный дашборд для Grafana + Prometheus доступен по следующей [ссылке](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-rabbitmq.json)
<img alt="Dashboard" src="../../images/dashboard-rabbitmq.jpg"><br>
