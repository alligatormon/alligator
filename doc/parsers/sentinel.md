# Redis Sentinel

Alligator collects Redis Sentinel metrics from the `INFO` command over the Redis protocol.

## Configuration

```
aggregate {
    sentinel tcp://127.0.0.1:26379;
}
```

If authentication is configured on the aggregate URL, the collector sends `AUTH` before `INFO`.

## Exported metrics

| Metric | Type | Description |
|--------|------|-------------|
| `sentinel_*` | gauge | Numeric fields from the Sentinel `INFO` output. Keys already prefixed with `sentinel_` in the source are exported as `sentinel_sentinel_*`. |
| `sentinel_status` | gauge | Master monitor status (`ok` or `fail`). |
| `sentinel_slaves` | gauge | Number of replicas for a monitored master. |
| `sentinel_sentinels` | gauge | Number of Sentinel processes monitoring a master. |

Labeled metrics use `name` and `address` labels parsed from the `master0:` section.

All exported families include Prometheus `# HELP` and `# TYPE gauge` metadata.

## Example

Input fragment:

```
sentinel_masters:1
master0:name=mymaster,status=ok,address=127.0.0.1:26379,slaves=2,sentinels=3
```

Produces metrics such as:

```
# HELP sentinel_sentinel_masters Redis Sentinel exported metric value.
# TYPE sentinel_sentinel_masters gauge
sentinel_sentinel_masters 1
# HELP sentinel_slaves Redis Sentinel exported metric value.
# TYPE sentinel_slaves gauge
sentinel_slaves{name="mymaster",address="127.0.0.1:26379"} 2
```
