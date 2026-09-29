## ElasticSearch

To enable the collection of statistics from ES, use the following option:
```
aggregate {
    elasticsearch http://localhost:9200;
}
```

If authentication is enabled, the user and password can be specified in the URL:
```
aggregate {
    elasticsearch http://user:password@localhost:9200;
}
```

The aggregator scrapes six endpoints and keeps the `elasticsearch_` metric prefix:

| Handler | Endpoint |
| --- | --- |
| nodes | `/_nodes/stats` |
| cluster | `/_cluster/stats` |
| health | `/_cluster/health?level=shards` |
| index | `/_stats` |
| settings | `/_settings` |
| cluster settings | `/_cluster/settings?include_defaults=true&flat_settings=true` |

Notable series (non-exhaustive):

- Cluster health: `elasticsearch_cluster_status{status}`, `elasticsearch_timed_out`, shard/index counts
- Index/shard status: `elasticsearch_indice_status{status}` / `elasticsearch_indice_shard_status{status}` emit green/yellow/red as `0`/`1`
- Nodes: `elasticsearch_nodes_{total,successful,failed}`, `elasticsearch_node_role{role}`, per-path `elasticsearch_fs_path_{total,free,available}_bytes`
- JVM: `elasticsearch_jvm_*`, `elasticsearch_jvm_gc_collectors{gc,key}`, `elasticsearch_jvm_mem_pools_bytes{pool,key}`
- Disk watermarks: `elasticsearch_disk_watermark_{low,high,flood_stage}_{percent,bytes}`, `elasticsearch_disk_threshold_enabled`
- Index stats: `elasticsearch_shards_{total,successful,failed,skipped}`, `elasticsearch_primaries_*` / `elasticsearch_total_*` with a `key` label

String identity fields from `/_cluster/stats` are not exported as gauges. `timestamp`, `max_unsafe_auto_id_timestamp`, and `resource_usage_stats` are skipped (host CPU/memory come from Alligator `system` collectors).

Alligator also allows the pushing metric to ElasticSearch using `action` methods.\
It provides the capability to set an index name for ElasticSearch using a template. This option supports strftime formatting, allowing for dynamically changeable opportunities at runtime.\
An example of usage is provided below. In this example, metrics will be pushed to the ElasticSearch instance every 15 seconds:

```
scheduler {
  name sched-elastic;
  period 15s;
  datasource internal;
  action to-elastic;
}

action {
    name to-elastic;
    expr http://localhost:9200/_bulk;
    serializer elasticsearch;
    index_template alligator-%Y-%m-%d;
}
```

The `json-query` also supports the parsing JSON responses from various databases, including ElasticSearch.\
For example, it can be useful for requesting data from ElasticSearch and parse the responses into metrics:
```
aggregate {
    json_query 'http://localhost:9200/_search?q=something';
}
```

## Dashboard
The system dashboard for Grafana + Prometheus is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-elasticsearch.json)
<img alt="Dashboard" src="../images/dashboard-elasticsearch.jpg"><br>
