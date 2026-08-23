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

Queue and worker counters become `gearmand_*` metrics.

Optional process / socket checks:

```
system {
    process gearmand;
    services gearmand.service;
}

query {
    expr 'count by (src_port, process) (socket_stat{process="gearmand", src_port="4730"})';
    make socket_match;
    datasource internal;
}
```
