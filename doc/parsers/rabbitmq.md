**Language / Язык:** [English](rabbitmq.md) | [Русский](../ru/parsers/rabbitmq.md)

## RabbitMQ

Scrapes the RabbitMQ Management HTTP API. Handler key prefix is **`rabbitmq`** (several paths under one aggregate context):

```
aggregate {
    rabbitmq http://guest:guest@localhost:15672;
}
```

Endpoints used: `/api/overview`, `/api/nodes`, `/api/exchanges`, `/api/connections`, `/api/queues`, `/api/vhosts`.

### Metrics (high level)

| Area | Examples | Notes |
|------|----------|--------|
| Overview | `rabbitmq_*` via `json_query` prefix | Raw overview fields under `rabbitmq_` |
| Nodes | `rabbitmq_node_<field>{node}` | Mostly gauges from `/api/nodes` |
| Node GC queues | `rabbitmq_node_gc_queue_length_*{node}` | Nested `metrics_gc_queue_length` |
| Cluster links | `rabbitmq_node_cluster_link_{recv,sent}_bytes_total` | Counters |
| Queues | `rabbitmq_queue_*{name,vhost}`, `rabbitmq_queue_up` | `up` = state `running` |
| Queue message stats | `rabbitmq_queue_messages_*` | From `message_stats` |
| Connections | `rabbitmq_connection_*`, `rabbitmq_connection_up` | High cardinality (host/peer/user/…) |
| Exchanges | `rabbitmq_exchange_messages_published_{in,out}_total` | Publish counters |
| Vhosts | `rabbitmq_vhost_*` | |

There is no single `rabbitmq_up` scrape gauge; rely on `alligator_parser_ok{parser=~"rabbitmq.*"}` for scrape health. Many node/queue integers are still typed as gauges (including cumulative counters) — prefer rates carefully or follow-up remaps.

Connection metrics can be high-cardinality; restrict Management API visibility or scrape interval if needed.

It is also useful to check process statistics, running services and open ports:

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

The system dashboard for Grafana + Prometheus is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-rabbitmq.json)

<img alt="Dashboard" src="../images/dashboard-rabbitmq.jpg"><br>
