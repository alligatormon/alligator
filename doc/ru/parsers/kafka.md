**Language / Язык:** [English](../../parsers/kafka.md) | [Русский](kafka.md)

# Kafka

Парсер `kafka` ходит в **брокеры** по нативному протоколу через **librdkafka** (тот же подход, что у [kafka_exporter](https://github.com/danielqsj/kafka_exporter): metadata, offset'ы партиций, ISR/replicas, lag consumer-group).

Это **не** полная замена внутренним метрикам брокера. Большую часть JVM, request, network, log и replica-manager Kafka отдаёт только по **JMX**. Их нужно снимать [Prometheus jmx_exporter](https://github.com/prometheus/jmx_exporter) как sidecar (или Java agent) и забирать exposition парсером Alligator `prometheus_metrics`.

| Источник | Что получаете |
|----------|----------------|
| `kafka` (этот парсер, librdkafka) | Брокеры кластера, партиции топиков, high/low offset, leader/replicas/ISR, under-replicated, members/offset/lag consumer-group |
| `jmx_exporter` + `prometheus_metrics` | JMX брокера/JVM: heap, GC, CPU, `BytesInPerSec`, `MessagesInPerSec`, latencies запросов, log flush, replica fetchers, … |

## Configuration

```
aggregate {
    kafka kafka://127.0.0.1:9092;
}
```

Порт по умолчанию — `9092`. Дополнительные bootstrap-брокеры и опции клиента librdkafka — в query string URL (`name=value`, те же ключи, что у librdkafka):

```
aggregate {
    kafka kafka://127.0.0.1:9092?bootstrap.servers=127.0.0.1:9092,127.0.0.2:9092;
    kafka kafka://user:secret@127.0.0.1:9092?security.protocol=SASL_SSL&sasl.mechanism=SCRAM-SHA-256&ssl.ca.location=/etc/kafka/ca.pem;
}
```

Username/password в URL задают `sasl.username` / `sasl.password` и по умолчанию `SASL_PLAINTEXT` + `PLAIN`, пока query-опции их не переопределят.

Ключи только для Alligator (в librdkafka не передаются), смысл как у kafka_exporter:

| Ключ | Default | Смысл |
|------|---------|--------|
| `topic_filter` | `.*` | PCRE: топики, которые включать |
| `topic_exclude` | `^$` | PCRE: топики, которые пропускать |
| `group_filter` | `.*` | PCRE: consumer groups, которые включать |
| `group_exclude` | `^$` | PCRE: consumer groups, которые пропускать |

```
aggregate {
    kafka kafka://127.0.0.1:9092?topic_filter=^app&topic_exclude=^__&group_filter=^cg-;
}
```

Также полезно проверять процесс брокера, unit и listener:

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

Имена и labels совпадают с kafka_exporter.

| Metric | Labels | Description |
|--------|--------|-------------|
| `kafka_brokers` | — | Брокеры в кластере |
| `kafka_broker_info` | `id`, `address` | Идентичность брокера (`value=1`) |
| `kafka_topic_partitions` | `topic` | Число партиций |
| `kafka_topic_partition_current_offset` | `topic`, `partition` | High watermark |
| `kafka_topic_partition_oldest_offset` | `topic`, `partition` | Low watermark |
| `kafka_topic_partition_leader` | `topic`, `partition` | ID leader-брокера |
| `kafka_topic_partition_replicas` | `topic`, `partition` | Число replicas |
| `kafka_topic_partition_in_sync_replica` | `topic`, `partition` | Число ISR |
| `kafka_topic_partition_leader_is_preferred` | `topic`, `partition` | `1`, если leader — replicas[0] |
| `kafka_topic_partition_under_replicated_partition` | `topic`, `partition` | `1`, если ISR < replicas |
| `kafka_consumergroup_members` | `consumergroup` | Члены группы |
| `kafka_consumergroup_current_offset` | `consumergroup`, `topic`, `partition` | Committed offset |
| `kafka_consumergroup_current_offset_sum` | `consumergroup`, `topic` | Сумма committed offset |
| `kafka_consumergroup_lag` | `consumergroup`, `topic`, `partition` | `high watermark − committed` (`-1`, если неизвестно) |
| `kafka_consumergroup_lag_sum` | `consumergroup`, `topic` | Сумма lag |

Серии consumer-group появляются только если группы есть и клиент может прочитать committed offsets.

## JMX: sidecar jmx_exporter + prometheus_metrics

librdkafka-парсер оставьте для offset/lag. Рядом с брокером запустите [jmx_exporter](https://github.com/prometheus/jmx_exporter) и снимайте `http://127.0.0.1:<port>/metrics` через Alligator.

### 1. Включить JMX на брокере

```
export JMX_PORT=9999
export KAFKA_JMX_OPTS="-Dcom.sun.management.jmxremote=true \
  -Dcom.sun.management.jmxremote.authenticate=false \
  -Dcom.sun.management.jmxremote.ssl=false \
  -Djava.rmi.server.hostname=127.0.0.1 \
  -Djava.net.preferIPv4Stack=true"
```

JMX лучше слушать на localhost, если Alligator и exporter на том же хосте.

### 2. Sidecar (standalone HTTP)

Режим collector: процесс exporter снимает локальный JMX и отдаёт Prometheus text.

`/etc/jmx_exporter/kafka.yml` (минимум; лучше официальный Kafka example из репозитория jmx_exporter):

```yaml
hostPort: 127.0.0.1:9999
lowercaseOutputName: true
rules:
  - pattern: ".*"
```

```
java -jar /opt/jmx_exporter/jmx_prometheus_standalone.jar 5556 /etc/jmx_exporter/kafka.yml
```

Exporter слушает `127.0.0.1:5556` (или `0.0.0.0:5556` в зависимости от версии/флагов). Проверка: `curl -s http://127.0.0.1:5556/metrics`.

### 3. Java agent в той же JVM (не отдельный sidecar)

```
export KAFKA_OPTS="$KAFKA_OPTS -javaagent:/opt/jmx_exporter/jmx_prometheus_javaagent.jar=5556:/etc/jmx_exporter/kafka.yml"
```

URL scrape тот же, что у sidecar.

### 4. Сбор Alligator

```
aggregate {
    kafka kafka://127.0.0.1:9092;
    prometheus_metrics http://127.0.0.1:5556/metrics;
}
```

`prometheus_metrics` — passthrough: имена и labels такие, как отдаёт jmx_exporter (`jvm_*`, `kafka_server_*`, …). См. [prometheus_metrics.md](prometheus_metrics.md).

Не открывайте JMX и порт exporter наружу; держите оба на loopback и пусть Alligator (или локальный Prometheus) забирает `http://127.0.0.1:…`.
