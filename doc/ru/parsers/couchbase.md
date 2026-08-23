**Language / Язык:** [English](../../parsers/couchbase.md) | [Русский](couchbase.md)

## Couchbase

Собирает статистику bucket, node и cluster через Couchbase REST API.

### Connection URL

```
http://[user:pass@]host[:8091]
```

Порт по умолчанию — **8091**. HTTP basic auth указывается в URL.

### Пример

```
aggregate {
    couchbase http://user:pass@localhost:8091;
}
```

См. [`src/tests/mock/couchbase/alligator.conf`](../../../src/tests/mock/couchbase/alligator.conf).

### Metrics

Префиксы:

- `couchbase_bucket_*`, `couchbase_bucket_node_*`, `couchbase_bucket_stat_*`
- `couchbase_node_stat_*`, `couchbase_stat_*`, `couchbase_task_*`

Метки: `name`, `bucketType`, `uuid`, `hostname`, `nodeUUID`.

Parser сам обнаруживает buckets и nodes и делает fan-out REST-запросов.
