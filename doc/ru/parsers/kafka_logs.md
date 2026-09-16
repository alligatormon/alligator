**Language / Язык:** [English](../../parsers/kafka_logs.md) | [Русский](kafka_logs.md)

# Потребление логов из Kafka

Потребление **сообщений топика** через librdkafka с передачей каждого payload в существующий stream-парсер: `grok`, `mtail`, `vrl`, `prometheus_metrics` или `log`.

Это **транспорт** aggregator, а не отдельный ключ парсера. Метрики кластера брокера — по-прежнему парсер [`kafka`](kafka.md) (URL без пути топика).

**Публикация** логов (Alligator → Kafka) настраивается в `log_channel` — см. [configuration.md](../configuration.md).

## URL

```
kafka://brokers[:port]/topic[?librdkafka_options]
```

| Часть | Смысл |
|-------|--------|
| brokers | Bootstrap-хост (порт по умолчанию `9092`) |
| topic | Обязательный сегмент пути |
| query | Опции librdkafka (`group.id`, `security.protocol`, …) |

Значения по умолчанию, если не заданы в query string:

| Опция | По умолчанию |
|-------|----------------|
| `group.id` | `alligator` |
| `client.id` | `alligator-kafka-consumer` |
| `auto.offset.reset` | `latest` |
| `enable.auto.commit` | `true` |

Username/password в URL задают `sasl.username` / `sasl.password` и по умолчанию `SASL_PLAINTEXT` + `PLAIN`, если query их не переопределяет.

Каждый payload — один chunk в multiparser. Если payload не заканчивается `\n`, Alligator дописывает перевод строки, чтобы топики «одно сообщение = одна строка» работали с linebuf grok/mtail/vrl. Multiline на aggregate по-прежнему применяется внутри многострочного сообщения. JSON-обёртки `log_channel` автоматически не снимаются — используйте VRL или grok.

## Примеры

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

Текст OpenMetrics / Prometheus в топике:

```
aggregate {
    prometheus_metrics kafka://127.0.0.1:9092/prom-text?group.id=alligator-prom;
}
```

### log (сырой forward)

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

## См. также

- Метрики брокера: [kafka.md](kafka.md)
- Транспорты aggregator: [aggregate.md](../aggregate.md)
- Grok: [grok.md](../grok.md)
- VRL: [vrl/README.md](../vrl/README.md)
- Prometheus text: [prometheus_metrics.md](prometheus_metrics.md)
