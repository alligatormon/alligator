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

Global directives (outside blocks): `log_level`, `log_dest`, `ttl`, `aggregate_period`, `query_period`, `system_collect_period`, and related timing options.

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

Destination can be standard streams of a Unix OS, a file, or a UDP port. For example, this directive can be set as follows:
```
- stdout
- stderr
- file:///var/log/messages
- udp://127.0.0.1:514
```

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

