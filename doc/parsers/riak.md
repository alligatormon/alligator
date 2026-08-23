**Language / Язык:** [English](riak.md) | [Русский](../ru/parsers/riak.md)

## Riak

Scrapes Riak HTTP stats (default port **8098**).

### Connection URL

```
http://[user:pass@]host[:8098]
```

### Example

```
aggregate {
    riak http://localhost:8098;
}
```

### Metrics

Riak `/stats` JSON fields become `riak_*` metrics.

Optional process / socket checks:

```
system {
    services riak.service;
}

query {
    expr 'count by (src_port, process) (socket_stat{src_port="8098"})';
    make socket_match;
    datasource internal;
}
```
