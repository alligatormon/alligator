**Language / Язык:** [English](opentsdb.md) | [Русский](../ru/parsers/opentsdb.md)

## OpenTSDB

Scrapes the OpenTSDB stats HTTP API.

### Connection URL

```
http://[user:pass@]host[:4242]/api/stats
```

The `/api/stats` path must be included in the URL. Default port is **4242**.

### Example

```
aggregate {
    opentsdb http://localhost:4242/api/stats;
}
```

See [`src/tests/mock/opentsdb/alligator.conf`](../../src/tests/mock/opentsdb/alligator.conf).

### Metrics

Each TSD stat becomes `opentsdb_<name>`. Latency-related metrics may include a `latency` label.
