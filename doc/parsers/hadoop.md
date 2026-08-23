**Language / Язык:** [English](hadoop.md) | [Русский](../ru/parsers/hadoop.md)

## Hadoop

Collects Hadoop DataNode (or similar) JMX beans over HTTP.

### Connection URL

```
http://[user:pass@]host:port/jmx
```

The JMX path must be present in the URL (for example port **50075** on classic DataNode HTTP).

### Example

```
aggregate {
    hadoop http://localhost:50075/jmx;
}
```

See [`src/tests/system/hadoop/alligator.yaml`](../../src/tests/system/hadoop/alligator.yaml).

### Metrics

Each numeric JMX bean field becomes `Hadoop_<field_name>`. Optional label `modelerType` is taken from the bean when present.
