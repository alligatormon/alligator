# ZooKeeper

Alligator collects ZooKeeper metrics using the four-letter words `mntr`, `isro`, and `wchs`.

## Configuration

```
aggregate {
    zookeeper tcp://127.0.0.1:2181;
}
```

It is also useful to check process statistics, running services and open ports:

```
system {
    process /zookeeper/;
    services zookeeper.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="java", src_port="3888"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="java", src_port="2181"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="java", src_port="2888"})';
	make socket_match;
	datasource internal;
}
```

## Exported metrics

| Metric | Type | Source | Description |
|--------|------|--------|-------------|
| `zk_*` | gauge | `mntr` | Numeric fields from the `mntr` output. |
| `zk_mode` | gauge | `mntr` | Node role: `standalone`, `follower`, or `leader`. |
| `zk_readwrite` | gauge | `isro` | Read/write mode: `ro`, `rw`, or `null`. |
| `zk_total_watches` | gauge | `wchs` | Total number of watches. |

All exported families include Prometheus `# HELP` and `# TYPE gauge` metadata.

## Example

`mntr` fragment:

```
zk_avg_latency	10
zk_server_state	leader
```

`isro` response:

```
rw
```

`wchs` fragment:

```
Total watches:15
```

## Dashboard

The system dashboard for Grafana + Prometheus is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-zookeeper.json)

<img alt="Dashboard" src="/doc/images/dashboard-zookeeper.jpg"><br>
