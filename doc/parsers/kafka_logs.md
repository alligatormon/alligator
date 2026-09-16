**Language / Язык:** [English](kafka_logs.md) | [Русский](../ru/parsers/kafka_logs.md)

# Kafka log consume

Consume **topic messages** over librdkafka and feed each payload into an existing stream parser: `grok`, `mtail`, `vrl`, `prometheus_metrics`, or `log`.

This is an aggregator **transport**, not a separate parser key. Broker cluster metrics remain the dedicated [`kafka`](kafka.md) parser (URL without a topic path).

Log **produce** (Alligator → Kafka) is configured on `log_channel` — see [configuration.md](../configuration.md).

## URL

```
kafka://brokers[:port]/topic[?librdkafka_options]
```

| Part | Meaning |
|------|---------|
| brokers | Bootstrap host (default port `9092`) |
| topic | Required path segment |
| query | Passed to librdkafka (`group.id`, `security.protocol`, …) |

Defaults when not set in the query string:

| Option | Default |
|--------|---------|
| `group.id` | `alligator` |
| `client.id` | `alligator-kafka-consumer` |
| `auto.offset.reset` | `latest` |
| `enable.auto.commit` | `true` |

Username/password in the URL set `sasl.username` / `sasl.password` and default to `SASL_PLAINTEXT` + `PLAIN` unless query options override them.

Each message payload is one chunk into the multiparser. If the payload does not end with a newline, Alligator appends `\n` so one-message-per-line topics work with grok/mtail/vrl line buffering. Multiline options on the aggregate still apply inside a multi-line message. JSON envelopes from Alligator `log_channel` are not unwrapped automatically — use VRL or grok on the payload.

## Examples

### grok

```
grok_patterns /etc/grok-patterns/patterns.conf;
grok {
    key app_line;
    name app_line;
    match '^%{WORD:level} %{GREEDYDATA:message}$';
}

aggregate {
    grok kafka://127.0.0.1:9092/app-logs?group.id=alligator-grok name=app_line;
}
```

### mtail

```
mtail {
    name nginx_mtail;
    script /etc/alligator/mtail/nginx.mtail;
}

aggregate {
    mtail kafka://127.0.0.1:9092/nginx-logs?group.id=alligator-mtail name=nginx_mtail;
}
```

### vrl

```
vrl {
    name remap;
    script /etc/vrl/app.vrl;
}

aggregate {
    vrl kafka://127.0.0.1:9092/app-logs?group.id=alligator-vrl name=remap
        start_pattern=^\S
        condition_pattern=^\s
        multiline_mode=continue_through;
}
```

### prometheus_metrics

OpenMetrics / Prometheus text published on a topic:

```
aggregate {
    prometheus_metrics kafka://127.0.0.1:9092/prom-text?group.id=alligator-prom;
}
```

### log (raw forward)

```
log_channel {
    name out;
    dest file:///var/log/alligator-raw.log;
}

aggregate {
    log kafka://127.0.0.1:9092/raw?group.id=alligator-raw log_channel_raw=out;
}
```

### SASL

```
aggregate {
    grok kafka://user:secret@broker:9092/app-logs?group.id=alligator-grok&security.protocol=SASL_SSL&sasl.mechanism=SCRAM-SHA-256&ssl.ca.location=/etc/kafka/ca.pem name=app_line;
}
```

## Related

- Broker metrics: [kafka.md](kafka.md)
- Aggregator transports: [aggregate.md](../aggregate.md)
- Grok: [grok.md](../grok.md)
- VRL: [vrl/README.md](../vrl/README.md)
- Prometheus text: [prometheus_metrics.md](prometheus_metrics.md)
