**Language / Язык:** [English](kafka.md) | [Русский](../ru/parsers/kafka.md)

# Kafka

The `kafka` parser talks to Kafka **brokers** over the native protocol using **librdkafka** (the same approach as [kafka_exporter](https://github.com/danielqsj/kafka_exporter): metadata, partition offsets, ISR/replicas, consumer-group lag).

This is **not** a full substitute for broker internals. Kafka exposes most JVM, request, network, log, and replica-manager metrics only over **JMX**. Collect those with [Prometheus jmx_exporter](https://github.com/prometheus/jmx_exporter) as a sidecar (or Java agent) and scrape the exposition with Alligator `prometheus_metrics`.

| Source | What you get |
|--------|----------------|
| `kafka` (this parser, librdkafka) | Cluster brokers, topic partitions, high/low offsets, leader/replicas/ISR, under-replicated flag, consumer-group members/offsets/lag |
| `jmx_exporter` + `prometheus_metrics` | Broker/JVM JMX: heap, GC, CPU, `BytesInPerSec`, `MessagesInPerSec`, request latencies, log flush, replica fetchers, … |

## Configuration

```
aggregate {
    kafka kafka://127.0.0.1:9092;
}
```

Default port is `9092`. Extra bootstrap brokers and librdkafka client options go in the URL query string (same `name=value` keys as librdkafka):

```
aggregate {
    kafka kafka://127.0.0.1:9092?bootstrap.servers=127.0.0.1:9092,127.0.0.2:9092;
    kafka kafka://user:secret@127.0.0.1:9092?security.protocol=SASL_SSL&sasl.mechanism=SCRAM-SHA-256&ssl.ca.location=/etc/kafka/ca.pem;
}
```

Username/password in the URL set `sasl.username` / `sasl.password` and default to `SASL_PLAINTEXT` + `PLAIN` unless query options override them.

Alligator-only query keys (not passed to librdkafka), same meaning as kafka_exporter:

| Key | Default | Meaning |
|-----|---------|---------|
| `topic_filter` | `.*` | PCRE: topics to include |
| `topic_exclude` | `^$` | PCRE: topics to skip |
| `group_filter` | `.*` | PCRE: consumer groups to include |
| `group_exclude` | `^$` | PCRE: consumer groups to skip |

```
aggregate {
    kafka kafka://127.0.0.1:9092?topic_filter=^app&topic_exclude=^__&group_filter=^cg-;
}
```

It is also useful to check the broker process, unit, and listener:

```
system {
    process /kafka/;
    services kafka.service;
}

query {
    expr 'count by (src_port, process) (socket_stat{process="java", src_port="9092"})';
    make socket_match;
    datasource internal;
}
```

## Exported metrics

Metric names and labels match kafka_exporter.

| Metric | Labels | Description |
|--------|--------|-------------|
| `kafka_brokers` | — | Brokers in the cluster |
| `kafka_broker_info` | `id`, `address` | Broker identity (`value=1`) |
| `kafka_topic_partitions` | `topic` | Partition count |
| `kafka_topic_partition_current_offset` | `topic`, `partition` | High watermark |
| `kafka_topic_partition_oldest_offset` | `topic`, `partition` | Low watermark |
| `kafka_topic_partition_leader` | `topic`, `partition` | Leader broker id |
| `kafka_topic_partition_replicas` | `topic`, `partition` | Replica count |
| `kafka_topic_partition_in_sync_replica` | `topic`, `partition` | ISR count |
| `kafka_topic_partition_leader_is_preferred` | `topic`, `partition` | `1` if leader is replicas[0] |
| `kafka_topic_partition_under_replicated_partition` | `topic`, `partition` | `1` if ISR < replicas |
| `kafka_consumergroup_members` | `consumergroup` | Members in the group |
| `kafka_consumergroup_current_offset` | `consumergroup`, `topic`, `partition` | Committed offset |
| `kafka_consumergroup_current_offset_sum` | `consumergroup`, `topic` | Sum of committed offsets |
| `kafka_consumergroup_lag` | `consumergroup`, `topic`, `partition` | `high watermark − committed` (`-1` if unknown) |
| `kafka_consumergroup_lag_sum` | `consumergroup`, `topic` | Sum of lag |

Consumer-group series appear only when groups exist and the client can list committed offsets.

## JMX: jmx_exporter sidecar + prometheus_metrics

Keep the librdkafka parser for offsets/lag. Run [jmx_exporter](https://github.com/prometheus/jmx_exporter) next to the broker and let Alligator scrape `http://127.0.0.1:<port>/metrics`.

### 1. Enable JMX on the broker

```
export JMX_PORT=9999
export KAFKA_JMX_OPTS="-Dcom.sun.management.jmxremote=true \
  -Dcom.sun.management.jmxremote.authenticate=false \
  -Dcom.sun.management.jmxremote.ssl=false \
  -Djava.rmi.server.hostname=127.0.0.1 \
  -Djava.net.preferIPv4Stack=true"
```

Bind JMX to localhost when Alligator and the exporter share the host.

### 2. Sidecar (standalone HTTP)

Collector mode: the exporter process scrapes local JMX and serves Prometheus text.

`/etc/jmx_exporter/kafka.yml` (minimal; prefer the official Kafka example from the jmx_exporter repo):

```yaml
hostPort: 127.0.0.1:9999
lowercaseOutputName: true
rules:
  - pattern: ".*"
```

```
java -jar /opt/jmx_exporter/jmx_prometheus_standalone.jar 5556 /etc/jmx_exporter/kafka.yml
```

The exporter listens on `127.0.0.1:5556` (or `0.0.0.0:5556` depending on version/flags). Check with `curl -s http://127.0.0.1:5556/metrics`.

### 3. Same-JVM Java agent (not a separate sidecar)

```
export KAFKA_OPTS="$KAFKA_OPTS -javaagent:/opt/jmx_exporter/jmx_prometheus_javaagent.jar=5556:/etc/jmx_exporter/kafka.yml"
```

Same scrape URL as the sidecar.

### 4. Alligator scrape

```
aggregate {
    kafka kafka://127.0.0.1:9092;
    prometheus_metrics http://127.0.0.1:5556/metrics;
}
```

`prometheus_metrics` is a passthrough: names and labels are whatever jmx_exporter emits (`jvm_*`, `kafka_server_*`, …). See [prometheus_metrics.md](prometheus_metrics.md).

Do not scrape a public JMX or exporter port; keep both on loopback and let Alligator (or a local Prometheus) pull `http://127.0.0.1:…`.
