# Cluster
Alligator supports two types of cluster configurations: one for receiving metrics and the other for collecting metrics.
<br>
<p align="center">
<h1 align="center" style="border-bottom: none">
    <img alt="alligator-cluster-entrypoint" src="images/cluster-entr.jpeg"></a><br>
</h1>
<br>
If the cluster is enabled in the aggregate context, it allows for lock-based clustering.
<br>
<h1 align="center" style="border-bottom: none">
    <img alt="alligator-cluster-aggregate" src="images/cluster-aggr.jpeg"></a><br>
</h1>

<br>
<br>
</p>

The Cluster enables the multi-node capabilities and can be used in two directions - as a metric receiver or as a metric crawler. For gauges pushed from several hosts, see [receiving metrics](metric-receive.md#cluster-gauges-and-several-hosts).\
To set up metric receiver cluster, each node have to be configured in the entrypoint as follows:
```
entrypoint {
    tcp 1111;
    cluster repl;
    instance srv1.example.com:1111;
}
```

To set up a metric crawler with an exclusive lock, each node should configure the aggregate context as follows:
```
aggregate {
    elasticsearch http://localhost:9200/ cluster=repl instance=srv1.example.com:1111;
}
```


## name
Specifies the name of the cluster, which can be used as a reference in the aggregate, entrypoint, or in the API.


## type
Available values:
- oplog
- sharedlock

The sharedlock value can be used to in aggregate context. This allows making outgoing requests exactly once through the cluster.
Oplog is an option that allows receiving and destributing metrics through the cluster nodes.

Pushed samples are Maglev-sharded by `sharding_key` (default `__name__`). The primary holder exposes them from the default namespace on `/metrics`. Extra `replica_factor` copies live in namespace `cluster_name:primary_peer` for HA and are scraped from that node only if the primary peer's sync TTL has expired (failover). Prometheus should scrape every cluster member; under a healthy cluster each series appears from one Maglev primary.


## server
Specifies all hosts of the cluster.


## size
Default: 1000
Specifies the oplog size. Minimum usable value is **2** (`begin==end` means empty, so capacity is `size-1`). Invalid values are rejected and fall back to 1000. If the operation log is full, each new insert drops **one** oldest entry (circular buffer), not the whole log. Each such drop increments `alligator_cluster_oplog_overflow_total{cluster,peer}` in the default namespace (not subject to entrypoint/push TTL).

Each `/oplog` POST shift (and GET peek) returns at most **1 MiB** of pending entries; the remainder stays in the ring for the next sync. This keeps the peer event loop from applying a multi‑tens‑of‑MB backlog in one shot.

Replication volume (also default namespace; updated ~1s from atomics):

- `alligator_cluster_oplog_enqueued_total{cluster,peer}` — samples appended into the oplog destined for `peer` (on the node that accepted the push)
- `alligator_cluster_oplog_applied_total{cluster,peer}` — samples applied from an `/oplog` sync pulled from `peer`
- `alligator_cluster_oplog_overflow_total{cluster,peer}` — oldest-entry drops when that peer ring is full


## shardinag\_key
Default: `__name__`

Specifies the sharding key to distribute keys through the cluster nodes.


## replica\_factor
Improves the availability metric if some of nodes go down.


# Example
```
cluster {
    name repl;
    size 10000;
    sharding_key __name__;
    replica_factor 2;
    type oplog;
    servers  srv1.example.com:1111 srv2.example.com:1111 srv3.example.com:1111;
}
```
