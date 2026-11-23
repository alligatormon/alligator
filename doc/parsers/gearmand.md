**Language / Язык:** [English](gearmand.md) | [Русский](../ru/parsers/gearmand.md)

## Gearmand

Collects Gearman job server status over TCP (default port **4730**).

### Connection URL

```
tcp://host[:4730]
```

### Example

```
aggregate {
    gearmand tcp://localhost:4730;
}
```

### Metrics

Queue and worker gauges become `gearmand_*` metrics (`gearmand_jobs` / `gearmand_server_jobs`, plus running/available_workers).

Optional process / socket checks:

```
system {
    process gearmand;
    services gearmand.service;
}

query {
    expr 'count by (src_port, process) (socket_listening{process="gearmand", src_port="4730"})';
    make socket_match;
    datasource internal;
}
```
