# Configuration

## Documentation index

| Topic | Document |
|-------|----------|
| Metric collection (pull) | [aggregate.md](aggregate.md) |
| Metric ingestion (push) and scrape | [entrypoint.md](entrypoint.md) |
| Host metrics | [system.md](system.md) |
| PromQL / SQL queries | [query.md](query.md) |
| Export and automation | [action.md](action.md), [scheduler.md](scheduler.md) |
| Metric namespaces | [namespace.md](namespace.md) |
| Log parsing | [grok.md](grok.md), [mtail/README.md](mtail/README.md) |
| Custom modules | [lang.md](lang.md) |
| DNS resolution checks | [resolver.md](resolver.md) |
| TLS certificate monitoring | [x509.md](x509.md) |
| Cluster replication | [cluster.md](cluster.md) |
| Service discovery | [service-discovery.md](service-discovery.md) |
| HTTP API | [api.md](api.md) |
| Puppeteer / browser checks | [puppeteer.md](puppeteer.md) |
| Thread pools | [threaded-loop.md](threaded-loop.md) |

### Top-level configuration contexts

These blocks can appear in `/etc/alligator.conf` (plain) or JSON configuration:

| Context | Purpose |
|---------|---------|
| `entrypoint` | Listen addresses, handlers, scrape/export endpoints |
| `aggregate` | Poll external targets and run parsers |
| `system` | Collect host and container metrics |
| `query` | Run PromQL or SQL on a schedule |
| `action` | Export metrics or run commands |
| `scheduler` | Trigger actions on an interval |
| `resolver` | DNS servers for blackbox/resolver checks |
| `namespace` | Declare metric namespace prefixes |
| `grok` | Register grok pattern sets |
| `mtail` | Register mtail programs |
| `lang` | Load external language modules |
| `probe` | Prometheus-style probe modules (HTTP/TCP checks via API) |
| `x509` | Monitor certificate files |
| `cluster` / `instance` | Cluster membership |
| `puppeteer` | Headless browser automation |
| `threaded_loop` | Named worker thread pools |

Global directives (outside blocks): `log_level`, `log_dest`, `log_channel`, `ttl`, `aggregate_period`, `query_period`, `system_collect_period`, and related timing options.

## Available log levels
- off
- fatal
- error
- warn
- info
- debug
- trace

## Log destinations
```
log_dest <dest>;
```

Destination can be standard streams of a Unix OS, a file, a UDP port, or a TCP socket. For example, this directive can be set as follows:
```
- stdout
- stderr
- file:///var/log/messages
- udp://127.0.0.1:514
- tcp://127.0.0.1:1514
- http://127.0.0.1:9200/alligator-logs/_bulk
- kafka://127.0.0.1:9092/alligator-logs
```

Kafka destinations (`kafka://brokers/topic`) publish logs asynchronously via `librdkafka`. Use them on named `log_channel` entries only; the global `log_dest` default channel is unchanged unless you explicitly configure it.

Broker list format: `host:port` or comma-separated `host1:port1,host2:port2`. Topic name is the URI path.
You can set optional `kafka_key` and `kafka_options` per channel, or pass options in URI query parameters.

Kafka channels are non-blocking. If the internal producer queue is full or the broker is unavailable, log lines are dropped and a throttled diagnostic is written to stderr.
When both URI query parameters and `kafka_options` are present, `kafka_options` values take precedence.

Recommended formats for Kafka consumers: `log_format json` or `log_format elastic`.

```json
{
  "log_channel": [
    {
      "name": "kafka-aggregate",
      "dest": "kafka://127.0.0.1:9092/alligator-aggregate-logs?key=aggregate",
      "kafka_options": {
        "acks": "all",
        "compression.type": "lz4",
        "linger.ms": 20
      },
      "log_format": "json",
      "log_time": true
    }
  ],
  "aggregate": [
    {
      "url": "tcp://127.0.0.1:9100",
      "handler": "prometheus",
      "log_channel": "kafka-aggregate"
    }
  ]
}
```

TCP log destinations connect asynchronously via libuv. If the remote side is not connected or a write is already in progress, log lines are dropped. Reconnection is retried in the background every few seconds.

HTTP destinations (`http://host:port/path`) POST logs to Elasticsearch-compatible endpoints using the bulk NDJSON protocol (`Content-Type: application/x-ndjson`). HTTP channels default to `log_format elastic`.

For Logstash TCP inputs with a JSON codec, use `tcp://` with `log_format elastic` — each log line is a JSON document terminated by newline.

`log_format json` writes one JSON object per line with `message`, `key` (from context `carg->key`), and `date` (channel timestamp). Works with any destination: stdout, file, UDP, TCP.

Optional per-channel fields for remote logging:

- `log_format` — `plain` (default for TCP/stdout/file), `json`, or `elastic` / `elasticsearch` / `ecs`
- `log_index` — Elasticsearch index name template, strftime-compatible (default `alligator-%Y.%m.%d`)
- `kafka_key` — partition key for Kafka destination
- `kafka_options` — object with `librdkafka` producer options (`name: value`)
  - JSON config: `"kafka_options": {"acks":"all","linger.ms":20}`
  - Plain config: repeat `kafka_options key:value;` inside `log_channel { ... }`

```json
{
  "log_channel": [
    {
      "name": "elk-http",
      "dest": "http://127.0.0.1:9200/alligator-logs/_bulk",
      "log_index": "alligator-%Y.%m.%d"
    },
    {
      "name": "logstash-tcp",
      "dest": "tcp://127.0.0.1:5044",
      "log_format": "elastic"
    },
    {
      "name": "json-file",
      "dest": "file:///var/log/alligator.json.log",
      "log_format": "json",
      "log_time": true
    }
  ]
}
```

## Log channels

Named log channels route context logs to different destinations. Global `log_dest` remains the default channel.

```json
{
  "log_dest": "stdout",
  "log_channel": [
    {"name": "aggregate", "dest": "file:///var/log/alligator-aggregate.log"},
    {"name": "system", "dest": "udp://127.0.0.1:514", "log_time": true}
  ],
  "aggregate": [
    {
      "url": "tcp://127.0.0.1:9100",
      "handler": "prometheus",
      "log_channel": "aggregate"
    }
  ]
}
```

Each channel accepts the same destination values as `log_dest`. Optional per-channel fields: `log_form`, `log_time`, `log_time_format`.

Context logs written via `carglog` are prefixed with `[context_key]` or `[channel_name/context_key]` when a named channel is used.

### Plain configuration: per-context log files

Define one channel per context, each writing to its own file under `/var/log/`, then reference the channel from that context:

```conf
# Global fallback log (startup, config parser, uncategorized messages)
log_level info;
log_dest file:///var/log/alligator.log;
log_time on;

# Channel definitions (one file per area)
log_channel {
    name aggregate;
    dest file:///var/log/alligator-aggregate.log;
    log_time on;
}

log_channel {
    name system;
    dest file:///var/log/alligator-system.log;
    log_time on;
}

log_channel {
    name entrypoint;
    dest file:///var/log/alligator-entrypoint.log;
    log_time on;
}

log_channel {
    name action;
    dest file:///var/log/alligator-action.log;
    log_time on;
}

log_channel {
    name remote-tcp;
    dest tcp://127.0.0.1:1514;
    log_time on;
}

log_channel {
    name elk;
    dest http://127.0.0.1:9200/alligator-logs/_bulk;
    log_index alligator-%Y.%m.%d;
    log_time on;
}

log_channel {
    name json-log;
    dest file:///var/log/alligator.json.log;
    log_format json;
    log_time on;
}

log_channel {
    name kafka-aggregate;
    dest kafka://127.0.0.1:9092/alligator-aggregate-logs;
    kafka_key aggregate;
    kafka_options acks:all;
    kafka_options compression.type:lz4;
    kafka_options linger.ms:20;
    log_format json;
    log_time on;
}

# --- contexts: pick channel + verbosity ---

system {
    base;
    network;
    log_level debug;
    log_channel system;
}

entrypoint {
    log_level debug;
    log_channel entrypoint;
    tcp 9100;
    handler prometheus;
}

aggregate {
    prometheus "http://127.0.0.1:9100/metrics" log_level=debug log_channel=aggregate;
    redis "tcp://localhost:6379" log_level=info log_channel=aggregate;
}

action {
    name export-metrics;
    serializer prometheus;
    expr http://127.0.0.1:9100/metrics;
    log_level trace;
    log_channel action;
}
```

`dest` and `log_dest` are equivalent inside a `log_channel` block. On aggregate URL lines use `log_channel=name`; inside `system`, `entrypoint`, `action` blocks use `log_channel name;`.

Query and other contexts that log via global `glog()` still use `log_dest` (the default channel).

## Available units for time data in configuration file:
- ms
- s
- h
- d
- w





## Comments in plain config
The plain configuration parser supports both single-line and multiline comments:
- `# comment text` for single-line comments.
- `/* comment text */` for multiline comments.


## /etc/alligator.conf
Below is an example of the structure of a configuration file for Alligator:
```
log_level <level>;
ttl <time>;

entrypoint {
    <options>;
}

resolver {
    <server1>;
    <server2>;
    ...
    <serverN>;
}

system {
    <option1>;
    <option2>;
    ...
    <optionN> [<param1>] ... [<paramN>];
}

aggregate {
    <option1>;
    <option2>;
    ...
    <optionN>;
}

query {
    <query1 options>;
}

query {
    <query2 options>;
}

action {
    <action1 options>;
}

action {
    <action2 options>;
}

scheduler {
    <scheduler1 options>;
}

scheduler {
    <scheduler2 options>;
}

x509 {
    <certificate options>;
}

x509 {
    <certificate options>;
}

puppeteer {
    <puppeteer options>;
}
```

# Support environment variables
`__` is a nesting separator of contexts
Example:
```
export ALLIGATOR__ENTRYPOINT0__TCP0=1111
export ALLIGATOR__ENTRYPOINT0__TCP1=1112
export ALLIGATOR__TTL=1200
export ALLIGATOR__LOG_LEVEL=0
export ALLIGATOR__AGGREGATE0__HANDLER=tcp
export ALLIGATOR__AGGREGATE0__URL="tcp://google.com:80"
```
converts to configuration:
```
{
  "entrypoint": [
    {
      "tcp": [
        "1111",
        "1112"
      ]
    }
  ],
  "ttl": "1200",
  "log_level": "0",
  "aggregate": [
    {
      "handler": "tcp",
      "url": "tcp://google.com:80"
    }
  ]
}
```

