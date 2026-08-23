**Language / Язык:** [English](../../parsers/hadoop.md) | [Русский](hadoop.md)

## Hadoop

Собирает JMX beans Hadoop DataNode (и аналоги) по HTTP.

### Connection URL

```
http://[user:pass@]host:port/jmx
```

Путь JMX должен быть в URL (например порт **50075** у классического DataNode HTTP).

### Пример

```
aggregate {
    hadoop http://localhost:50075/jmx;
}
```

См. [`src/tests/system/hadoop/alligator.yaml`](../../../src/tests/system/hadoop/alligator.yaml).

### Metrics

Каждое числовое поле JMX bean становится `Hadoop_<field_name>`. Опциональная метка `modelerType` берётся из bean, если есть.
