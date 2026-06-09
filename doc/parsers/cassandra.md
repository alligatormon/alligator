# Cassandra

Alligator supports Cassandra in two directions: **pull** metrics with SQL-like queries through the `cassandra` aggregate handler, and **push** metrics to Cassandra with the `cassandra` action serializer.

## Pull metrics from Cassandra

Register a datasource in `aggregate`, then reference it from `query` blocks:

```
aggregate {
    cassandra cassandra://user:password@127.0.0.1:9042 name=from-cassandra;
}

query {
    expr 'SELECT value, name FROM metric';
    field value;
    datasource from-cassandra;
}

query {
    expr "SELECT keyspace_name, table_name, bloom_filter_fp_chance, gc_grace_seconds, compaction FROM system_schema.tables";
    field bloom_filter_fp_chance gc_grace_seconds;
    make cas_db_size;
    datasource from-cassandra;
}
```

Numeric columns listed in `field` become metrics. Other columns can be used as labels when they are not listed in `field`.

## Push metrics to Cassandra

Export internal metrics on a schedule:

```
aggregate {
    cassandra cassandra://user:password@127.0.0.1/app_data name=to-cassandra;
}

scheduler {
    name cassandra;
    period 15s;
    datasource internal;
    action to-cassandra;
}

action {
    name to-cassandra;
    expr cassandra;
    serializer cassandra;
}
```

The `cassandra` serializer stores time-series samples in Cassandra. Use `index_template` on the action when you need time-based table or key names (strftime formatting is supported).

## Graphite and file exporters

Cassandra itself can expose metrics through Graphite or JMX/file exporters. Those streams can be collected with the usual Alligator parsers (`prometheus_metrics`, custom `exec://` scripts, and so on) without using the `cassandra` SQL handler.
