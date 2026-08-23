**Language / Язык:** [English](couchbase.md) | [Русский](../ru/parsers/couchbase.md)

## Couchbase

Collects bucket, node, and cluster statistics from the Couchbase REST API.

### Connection URL

```
http://[user:pass@]host[:8091]
```

Default port is **8091**. HTTP basic auth credentials go in the URL.

### Example

```
aggregate {
    couchbase http://user:pass@localhost:8091;
}
```

See [`src/tests/mock/couchbase/alligator.conf`](../../src/tests/mock/couchbase/alligator.conf).

### Metrics

Metrics use prefixes such as:

- `couchbase_bucket_*`, `couchbase_bucket_node_*`, `couchbase_bucket_stat_*`
- `couchbase_node_stat_*`, `couchbase_stat_*`, `couchbase_task_*`

Common labels: `name`, `bucketType`, `uuid`, `hostname`, `nodeUUID`.

The parser discovers buckets and nodes automatically and fans out REST requests.
