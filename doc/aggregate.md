# Aggregator
The aggregator feature provides the opportunity to collect metrics from other software.\
It consists of two main parts: `aggregator` and `parser`.

<br>
<p align="center">
<h1 align="center" style="border-bottom: none">
    <img alt="alligator-cluster-entrypoint" src="/doc/images/aggregator.jpeg"></a><br>
</h1>
<br>

Alligator eventloop provides asynchronous methods for collecting metrics from various sources and passing them to custom parsers for each software:
<br>
<h1 align="center" style="border-bottom: none">
    <img alt="alligator-cluster-aggregate" src="/doc/images/parsers.jpeg"></a><br>
</h1>

<br>
<br>
</p>

The aggregator indludes async methods to get stats using various schemas/protocols:
- HTTP (http://) and HTTPS (https://). Enables the HTTP and HTTPS clients to get body.
- TCP (tcp://). Enables the TCP client to get body.
- TLS (tls://). Enables the TLS client to get body.
- UDP (udp://). Enables the UDP client to get body.
- unix (unix://) and unixgram (unixgram://). Enables the Unix-socket over SOCK\_STREAM and SOCK\_DGRAM clients to get the body.
- file (file://). Enables the file read to get body.
- exec (exec://). Enables the execution of an external program and read the stdout to the parser.
- WebSocket (ws://) and WebSocket over TLS (wss://). Enables a persistent WebSocket client that passes each received text frame to the parser.

The parser gets the body after the aggregator works on it and parses it into metrics.

## Overview
```
aggregate {
    <parser1> <url1> [arg1] [arg2] ... [argN];
    <parser2> <url2> [arg1] [arg2] ... [argN];
    ...
    <parserM> <urlM> [arg1] [arg2] ... [argN];
}
```

## env
Default: -
Plural: yes

Specifies to request HTTP headers for HTTP/HTTPS protocols or environment variables for running external processes (exec).\
Here is an example of usage:
```
aggregate {
    blackbox https://google.com env=Connection:close env=User-agent:alligator;
}
```

## key
Default: automatically generates from the URL
Plural: no

Resets the key, a unique variable that usually generates automatically and is used as a primary key in the metrics connected to the aggregator's or API's working.


## bind\_address
Default: 0.0.0.0:{random port}, where random port is generated automatically by OS
Plural: no

Makes outgoing connections to the target server originate from a specific local address.
Supported formats are:
- `bind_address=<port>` or `bind_address=:<port>` - bind only by local port (IP defaults to `0.0.0.0`)
- `bind_address=<ip>` - bind only by local IP (port is chosen by OS)
- `bind_address=<ip>:<port>` - bind by both local IP and local port

For instance:

```
aggregate {
    blackbox https://example.com bind_address=1234;
    blackbox https://example.com bind_address=:1234;
    dns udp://8.8.8.8:53 resolve=google.com type=a add_label=check:dns bind_address=0.0.0.0;
    dns udp://8.8.4.4:53 resolve=yahoo.com type=a add_label=check:dns bind_address=192.0.2.1:1234;
}
```

## name
Default: -\
Plural: no

Sets the name of the current aggregate object. This name allows the query to refer to the current aggregator.


## lang
Default: -\
Plural: no

The [lang](https://github.com/alligatormon/alligator/blob/master/doc/lang.md) option specifies the context that operates the received body with custom functions via external modules.

# follow\_redirects
Default: 0
Plural: no

The follow\_redirects option specifies the maximum number of following redirects.

## tls\_certificate
Default: -\
Plural: no

Specifies the path of the client x509 certificate for creating mTLS sessions.

## tls\_key
Default: -\
Plural: no

Specifies the path of the client x509 key for creating TLS sessions.


## tls\_ca
Default: -\
Plural: no

Specifies the certification authority (CA) x509 certificate path to verify this TLS session.


## tls\_server\_name
Default: -\
Plural: no

Specifies the TLS servername field when it is necessary.


## timeout
Default: 5s\
Plural: no

Specifies the timeout for the request.
More information about units that user can specify in configuratino can be obtained from configuration [doc](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-units-for-time-data-in-configuration-file).


## calc\_lines
Default: false\
Plural: no

Enables the calculation of lines in files within the directory.


## checksum
Default: false\
Plural: no
Available values:
- murmur3
- crc32

Enables the calculation of checksums for files within the directory.


## notify
Default: false\
Plural: no\
Available values:
- true
- false
- only

Enables the inotify mechanisms to check for updates of files within the directory. When set to `only`, the directive runs the file reader using notifications only and disables the global file aggregator scheduler (`file_aggregator_repeat`). With `notify=false` (default), new file bytes are picked up on each global file crawl tick (`file_aggregator_repeat` in `system` config, default 10s); all pending lines since the last saved offset are read in one batch per tick and forwarded line by line. Do not confuse global `file_aggregator_repeat` with per-aggregate `period` (a separate per-file timer).


## state
Default: stream\
Plural: no\
Possible values:
- begin
- save
- stream
- forget

Enables different read initiation modes for Alligator, both on restart and during runtime:
- **begin**: On startup, reads the file from the beginning, then reads only appended data.
- **save**: Saves the file offset between Alligator restarts and reads only appended data.
- **stream**: Starts reading from the current end of the file and reads only appended data.
- **forget**: Always reads the file from the beginning.


## pingloop
Default: 0\
Plural: no\
Possible values:
- {number}

Pingloop allows a blackbox handler to ping resource more than once.


## log\_level
Default: off\
Plural: no

Specify of the level of logging for the aggregator. Units for this option are explained in this [document](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-log-levels)

## log\_channel\_raw
Default: -\
Plural: no

For file and socket transports (`file://`, `tcp://`, `udp://`, `unix://`, `unixgram://`, `tls://`), forwards incoming data **line by line** (newline-delimited) to the named log channel. Each line is kept intact inside `message`; channel `log_format` / `log_time` add JSON/elastic envelope or a timestamp prefix only. Use with `handler log` for log-only shipping, or alongside grok/mtail when metrics are parsed separately. See [configuration — raw stream passthrough](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#raw-stream-passthrough-log_channel_raw).

Plain example: `log "file:///var/log/app.log" log_channel_raw=kafka-raw;`


## threaded_loop_name
Default: -\
Plural: no

Specifies the declared name of the threaded loop. More information about the threaded loop can be found in the [threaded_loop](https://github.com/alligatormon/alligator/blob/master/doc/threaded-loop.md) documentation. This enables the thread pool for a particular aggregator and ensures that the main thread does not process it.


## stdin
Specifies the body to be passed to the stdin of the called script.


# cluster
Default: -\
Plural: no

Specify the cluster name for crawling metrics. More information about cluster can be found in the [cluster](https://github.com/alligatormon/alligator/blob/master/doc/cluster.md) documentation.

# instance
Default: -\
Plural: no

Specify the cluster current instance name for crawling metrics. More information about cluster can be found in [cluster](https://github.com/alligatormon/alligator/blob/master/doc/cluster.md) documentation.


## pquery
Default: -\
Plural: no

Specifies the JSON query for the json\_query parser. More information can be found in the [json\_query](https://github.com/alligatormon/alligator/blob/master/doc/parsers/json_query.md) documentation.

## period
Default: -\
Plural: no

Specifies the time interval for the repeated calling of the request.\
More information about units that user can specify in configuratino can be obtained from configuration [doc](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-units-for-time-data-in-configuration-file).


## resolve
Default: -\
Plural: no

Specify the domain name for resolution. More information about DNS requests in alligator can be found in [resolver](https://github.com/alligatormon/alligator/blob/master/doc/resolver.md) documentation.

## metricstransform
Default: -\
Plural: no

`metricstransform` rewrites **label keys and/or values** for metrics produced by this aggregate item; the result is stored in Alligator like entrypoint ingest transforms.
The rule format is the same OTel-style JSON used in other contexts (`transforms` -> `operations` -> `value_actions`, plus optional `new_label` / `label_key_actions` as in [action.md § metricstransform](https://github.com/alligatormon/alligator/blob/master/doc/action.md#metricstransform)).

In plain config, pass either **inline JSON** (`metricstransform='{...}'` or `metricstransform={...}`) or a **native block** after the handler and URL on the same aggregate line.

Inline JSON:
```
aggregate {
    prometheus_metrics http://127.0.0.1:9100/metrics metricstransform='{"transforms":[{"include":"^node_.*$","match_type":"regexp","operations":[{"action":"update_label","label":"instance","value_actions":[{"regex":"^([^:]+):?.*$","replacement":"$1"}]}]}]}';
}
```

Native block (same keyword grammar as [action.md § metricstransform](https://github.com/alligatormon/alligator/blob/master/doc/action.md#metricstransform)):
```
aggregate {
    prometheus_metrics http://127.0.0.1:9100/metrics metricstransform {
        include ^node_.*$ match_type regexp label instance regex '^([^:]+):?.*$' replacement '$1';
    };
}
```

This is useful for cardinality cleanup before storage and before exporting through actions/serializers.

When the same metrics are later exported via an **action** that uses `metric_name_transform`, [export-time `metricstransform`](https://github.com/alligatormon/alligator/blob/master/doc/action.md#matching-metric-names-include-metric-metric-regex) on that action can match either the stored name or the transformed export name; aggregate-time rules here only see the name as produced by the aggregate/parser.

## WebSocket transport (ws:// and wss://)

The `ws://` and `wss://` schemes connect to a WebSocket server and subscribe to its stream. The connection is **persistent** — alligator maintains it continuously and reconnects automatically if the server closes or drops the socket.

Each text frame received from the server is passed directly to the configured parser, the same way a TCP response body would be. Any parser that works over TCP also works over WebSocket.

Default ports: `80` for `ws://`, `443` for `wss://`.

### Reconnect behaviour

| Situation | Behaviour |
|-----------|-----------|
| Initial connection failed | Retry after `period` (default 10 s) |
| Server closed the connection | Reconnect after `period` |
| Server is unreachable | Retry after `period` |

Use `period` to control the reconnect interval.

### Typical use cases

**Prometheus metrics endpoint exposed over WebSocket** — useful when the target is behind a WebSocket proxy or a custom push-based exporter:

```
aggregate {
    prometheus_metrics ws://metrics-relay.internal:9100/metrics;
    prometheus_metrics wss://secure-relay.internal/metrics add_label=env:prod;
}
```

**Blackbox connectivity and availability check** — the WebSocket handshake itself acts as the probe; alligator emits TCP/TLS timing metrics even if no frames arrive:

```
aggregate {
    blackbox ws://api.example.com:8080/health add_label=service:api;
    blackbox wss://ws.example.com/status      add_label=service:ws-gateway;
}
```

**Prometheus metrics with label rewriting and reconnect interval:**

```
aggregate {
    prometheus_metrics ws://node-exporter-relay:9091/stream
        period=30s
        add_label=datacenter:dc1
        metricstransform='{"transforms":[{"include":"^node_.*$","match_type":"regexp","operations":[{"action":"update_label","label":"instance","value_actions":[{"regex":"^([^:]+):?.*$","replacement":"$1"}]}]}]}';
}
```

**Multiple WebSocket sources:**

```
aggregate {
    prometheus_metrics ws://relay-a.internal/metrics add_label=relay:a;
    prometheus_metrics ws://relay-b.internal/metrics add_label=relay:b;
    blackbox           wss://healthcheck.internal/ws  add_label=check:websocket;
}
```

### Notes

- The `period` option sets both the collection schedule and the reconnect delay after a disconnect. If omitted the default reconnect delay is 10 s.
- The WebSocket client sends a standard RFC 6455 upgrade handshake. The server must respond with HTTP 101. No subprotocol negotiation is performed.
- `wss://` (TLS) is parsed and registered; the TLS layer is on the roadmap — currently it connects without TLS even for `wss://` URLs. Use a TLS-terminating proxy in front of the target if you need encryption today.

## add_label
Default: -\
Plural: yes

Adds static labels to every metric produced by this aggregate entry. Format: `add_label=key:value`.

```
aggregate {
    elasticsearch http://localhost:9200 add_label=instance:localhost add_label=name:localcluster;
    elasticsearch http://external:9200 add_label=instance:external add_label=name:extcluster;
}
```

Also supported on entrypoints and actions. See [entrypoint add_label](https://github.com/alligatormon/alligator/blob/master/doc/entrypoint.md#add_label).

## namespace
Default: -\
Plural: no

Assigns metrics from this aggregate to a named namespace. See [namespace](https://github.com/alligatormon/alligator/blob/master/doc/namespace.md).

## no_collect
Default: false\
Plural: no

Keeps the aggregate entry in configuration but disables metric collection.

## ttl
Default: global `ttl`\
Plural: no

Overrides the metric TTL for this aggregate entry.

## file_stat
Default: false\
Plural: no

When reading files or directories, exports file metadata metrics (`file_stat_time`, `file_stat_size`, `file_stat_mode`, and related families). Used together with `blackbox file://` and similar handlers.

## script
Default: -\
Plural: no

Shell script executed for `exec://` aggregates. Output is passed to the parser.

## Available parsers

The first token in an `aggregate` line is the **handler key** registered in Alligator. It must match the code exactly (for example `named`, not `bind`; `nginx_upstream_check`, not `nginx`; `nvidia_smi`, not `nvidia-smi`).

### Parser documentation

**Databases and caches:** [redis](https://github.com/alligatormon/alligator/blob/master/doc/parsers/redis.md), [memcached](https://github.com/alligatormon/alligator/blob/master/doc/parsers/memcached.md), [MongoDB](https://github.com/alligatormon/alligator/blob/master/doc/parsers/mongodb.md), [mysql](https://github.com/alligatormon/alligator/blob/master/doc/parsers/mysql.md), [postgresql](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md) ([pgbouncer](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md#pgbouncer), [odyssey](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md#odyssey), [pgpool](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md#pgpool)), [clickhouse](https://github.com/alligatormon/alligator/blob/master/doc/parsers/clickhouse.md), [cassandra](https://github.com/alligatormon/alligator/blob/master/doc/parsers/cassandra.md), [couchbase](https://github.com/alligatormon/alligator/blob/master/doc/parsers/couchbase.md), [couchdb](https://github.com/alligatormon/alligator/blob/master/doc/parsers/couchdb.md), [riak](https://github.com/alligatormon/alligator/blob/master/doc/parsers/riak.md), [aerospike](https://github.com/alligatormon/alligator/blob/master/doc/parsers/aerospike.md)

**Messaging and queues:** [rabbitmq](https://github.com/alligatormon/alligator/blob/master/doc/parsers/rabbitmq.md), [beanstalkd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/beanstalkd.md), [nats](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nats.md), [gearmand](https://github.com/alligatormon/alligator/blob/master/doc/parsers/gearmand.md)

**Web and proxies:** [haproxy](https://github.com/alligatormon/alligator/blob/master/doc/parsers/haproxy.md), [nginx](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nginx.md) (`nginx_upstream_check`), [apache httpd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/apache-httpd.md) (`httpd`), [lighttpd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/lighttpd.md), [uwsgi](https://github.com/alligatormon/alligator/blob/master/doc/parsers/uwsgi.md), [varnish](https://github.com/alligatormon/alligator/blob/master/doc/parsers/varnish.md), [squid](https://github.com/alligatormon/alligator/blob/master/doc/parsers/squid.md)

**Search and analytics:** [elasticsearch](https://github.com/alligatormon/alligator/blob/master/doc/parsers/elasticsearch.md), [druid](https://github.com/alligatormon/alligator/blob/master/doc/parsers/druid.md) (`druid`, `druid_broker`, `druid_historical`, `druid_worker`), [hadoop](https://github.com/alligatormon/alligator/blob/master/doc/parsers/hadoop.md), [opentsdb](https://github.com/alligatormon/alligator/blob/master/doc/parsers/opentsdb.md), [eventstore](https://github.com/alligatormon/alligator/blob/master/doc/parsers/eventstore.md)

**DNS:** [named](https://github.com/alligatormon/alligator/blob/master/doc/parsers/named.md) (`named`), [powerdns](https://github.com/alligatormon/alligator/blob/master/doc/parsers/powerdns.md), [gdnsd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/gdnsd.md), [unbound](https://github.com/alligatormon/alligator/blob/master/doc/parsers/unbound.md), [nsd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nsd.md), [dns](https://github.com/alligatormon/alligator/blob/master/doc/resolver.md) (resolver checks)

**Infrastructure:** [kubernetes](https://github.com/alligatormon/alligator/blob/master/doc/parsers/kubernetes.md) (`kubernetes_operator`, `kubernetes_endpoint`, `kubernetes_ingress`, `kubeconfig`), [keepalived](https://github.com/alligatormon/alligator/blob/master/doc/parsers/keepalived.md), [monit](https://github.com/alligatormon/alligator/blob/master/doc/parsers/monit.md), [patroni](https://github.com/alligatormon/alligator/blob/master/doc/parsers/patroni.md), [zookeeper](https://github.com/alligatormon/alligator/blob/master/doc/parsers/zookeeper.md), [sentinel](https://github.com/alligatormon/alligator/blob/master/doc/parsers/sentinel.md), [snmp](https://github.com/alligatormon/alligator/blob/master/doc/parsers/snmp.md), [ipmi](https://github.com/alligatormon/alligator/blob/master/doc/parsers/ipmi.md), [wazuh](https://github.com/alligatormon/alligator/blob/master/doc/parsers/wazuh.md), [ntp](https://github.com/alligatormon/alligator/blob/master/doc/parsers/ntp.md), [nvidia-smi](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nvidia-smi.md) (`nvidia_smi`)

**Storage:** [mogilefs](https://github.com/alligatormon/alligator/blob/master/doc/parsers/mogilefs.md), [moosefs](https://github.com/alligatormon/alligator/blob/master/doc/parsers/moosefs.md)

**Observability and logs:** [syslog-ng](https://github.com/alligatormon/alligator/blob/master/doc/parsers/syslog-ng.md), [auditd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/auditd.md) (entrypoint `auditd`), [rsyslog impstats](https://github.com/alligatormon/alligator/blob/master/doc/parsers/rsyslog.md) (entrypoint `rsyslog-impstats`), [prometheus_metrics](https://github.com/alligatormon/alligator/blob/master/doc/parsers/prometheus_metrics.md), [json_query](https://github.com/alligatormon/alligator/blob/master/doc/parsers/json_query.md), [blackbox](https://github.com/alligatormon/alligator/blob/master/doc/parsers/blackbox.md), [tftp](https://github.com/alligatormon/alligator/blob/master/doc/parsers/tftp.md)

**Other:** Celery [flower](https://github.com/alligatormon/alligator/blob/master/doc/parsers/flower.md), [consul](https://github.com/alligatormon/alligator/blob/master/doc/service-discovery.md) (`consul`, `consul-configuration`, `consul-discovery`), [lang](https://github.com/alligatormon/alligator/blob/master/doc/lang.md), [grok](https://github.com/alligatormon/alligator/blob/master/doc/grok.md), [mtail](https://github.com/alligatormon/alligator/blob/master/doc/mtail/README.md), **log** (forward-only via `log_channel_raw`)

**Utility handlers (no dedicated parser page):** `http`, `tcp`, `process`, `influxdb` (scheduler export), `jsonparse`, `redis_ping`, `sphinxsearch`, `dummy`

## Example of usage with TLS options
```
aggregate {
    prometheus_metrics https://consulnode:8501/v1/agent/metrics?format=prometheus tls_ca=/etc/consul.d/ca.crt tls_certificate=/etc/consul.d/server.crt tls_key=/etc/consul.d/server.key tls_server_name=consulcluster env=X-Consul-Token:XXXX;
}
```

## Example of usage checking directory for changes:
```
aggregate {
    blackbox file:///etc/ calc_lines=true file_stat=true checksum=murmur3;
}
```

These configuration generates metrics for each file in the directory:
```
file_lines {path="/etc/fuse.conf"} 17
file_stat_time {type="birthtime", path="/etc/fuse.conf"} 1676453256
file_stat_time {type="mtime", path="/etc/fuse.conf"} 1648043594
file_stat_time {type="ctime", path="/etc/fuse.conf"} 1676453256
file_stat_mode {type="regular", user="root", path="/etc/fuse.conf", group="root", int="100644", mode="rw-r--r--"} 33188
file_stat_modify_count {path="/etc/fuse.conf"} 3
file_stat_size {path="/etc/fuse.conf"} 694
file_checksum {path="/etc/fuse.conf", hash="murmur3"} 1032033040
```

Please note that when parsing files in a directory, the directory path must end with '/' as follows: `file:///var/log/`. This enables the directory walk mode. By default, single file checking is enabled.


## Example of usage reading metrics from a file:
```
aggregate {
    prometheus_metrics file:///var/run/keepalived_time_state state=save notify=true;
}
```

## Period by default
The configuration file allows to specify the default period of time to check resources in aggregate context:
```
aggregate_period 10s;
```

* The [service\_discovery](https://github.com/alligatormon/alligator/blob/master/doc/service-discovery.md) allows for retrieving configuration from Consul or etcd instances.
