**Language / Язык:** [English](../../parsers/opentsdb.md) | [Русский](opentsdb.md)

## OpenTSDB

Собирает OpenTSDB stats HTTP API.

### Connection URL

```
http://[user:pass@]host[:4242]/api/stats
```

Путь `/api/stats` должен быть в URL. Порт по умолчанию — **4242**.

### Пример

```
aggregate {
    opentsdb http://localhost:4242/api/stats;
}
```

См. [`src/tests/mock/opentsdb/alligator.conf`](../../../src/tests/mock/opentsdb/alligator.conf).

### Metrics

Каждая TSD stat становится `opentsdb_<name>`. Latency-метрики могут иметь метку `latency`.
