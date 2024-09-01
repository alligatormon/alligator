<p align="center">
Alligator supports two types of cluster configurations: one for receiving metrics and the other for collecting metrics.
<br>
<h1 align="center" style="border-bottom: none">
    <img width="1024" height="480" alt="alligator-cluster-entrypoint" src="/doc/images/cluster-entr.jpeg"></a><br>
</h1>
<br>
If the cluster is enabled in the aggregate context, it allows for lock-based clustering.
<br>
<h1 align="center" style="border-bottom: none">
    <img width="1024" height="480" alt="alligator-cluster-aggregate" src="/doc/images/cluster-aggr.jpeg"></a><br>
</h1>

<br>
<br>
</p>

# Cluster
The Cluster enables the multi-node capabilities and can be used in two directions - as a metric receiver or as a metric crawler.\
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


## server
Specifies all hosts of the cluster.


## size
Default: 1000
Specifies the oplog size. If operation log overflows, old metrics in the oplog will be dropped.


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
