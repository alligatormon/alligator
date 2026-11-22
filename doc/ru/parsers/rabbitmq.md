**Language / Язык:** [English](../../parsers/rabbitmq.md) | [Русский](rabbitmq.md)

## RabbitMQ

Снимает RabbitMQ Management HTTP API. Ключ handler — **`rabbitmq`** (несколько путей в одном aggregate context):

```
aggregate {
    rabbitmq http://guest:guest@localhost:15672;
}
```

Эндпоинты: `/api/overview`, `/api/nodes`, `/api/exchanges`, `/api/connections`, `/api/queues`, `/api/vhosts`.

### Метрики (обзор)

| Область | Примеры | Примечание |
|---------|---------|------------|
| Overview | `rabbitmq_*` через `json_query` | Сырые поля overview |
| Nodes | `rabbitmq_node_<field>{node}` | В основном gauges из `/api/nodes` |
| GC queues | `rabbitmq_node_gc_queue_length_*{node}` | Вложенный `metrics_gc_queue_length` |
| Cluster links | `rabbitmq_node_cluster_link_{recv,sent}_bytes_total` | Counters |
| Queues | `rabbitmq_queue_*{name,vhost}`, `rabbitmq_queue_up` | `up` = state `running` |
| Queue message stats | `rabbitmq_queue_messages_*` | Из `message_stats` |
| Connections | `rabbitmq_connection_*`, `rabbitmq_connection_up` | Высокая кардинальность |
| Exchanges | `rabbitmq_exchange_messages_published_{in,out}_total` | Publish counters |
| Vhosts | `rabbitmq_vhost_*` | |

Отдельного `rabbitmq_up` нет — для здоровья scrape используйте `alligator_parser_ok{parser=~"rabbitmq.*"}`. Многие integer-поля node/queue всё ещё gauges (в т.ч. кумулятивные) — `rate()` применять осторожно.

Метрики connections могут быть высококардинальными; при необходимости ограничьте видимость Management API или период scrape.

Также полезно смотреть процесс, сервис и порты:

```
system {
    process beam.smp;
    services rabbitmq-server.service;
}

query {
    expr 'count by (src_port, process) (socket_listening{process="beam.smp", src_port="15672"})';
    make socket_match;
    datasource internal;
}
```

## Dashboard

Системный дашборд: [ссылка](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-rabbitmq.json)

<img alt="Dashboard" src="../../images/dashboard-rabbitmq.jpg"><br>
