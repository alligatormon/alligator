**Language / Язык:** [English](squid.md) | [Русский](../ru/parsers/squid.md)

## Squid

Scrapes Squid cache manager statistics over HTTP (default port **3128**).

### Connection URL

```
http://[user:pass@]host[:3128]
```

### Example

```
aggregate {
    squid http://localhost:3128;
}
```

### Metrics

Cache manager fields become `squid_*` metrics.

Optional process / socket checks:

```
system {
    process squid;
    services squid.service;
}

query {
    expr 'count by (src_port, process) (socket_stat{process="squid", src_port="3128"})';
    make socket_match;
    datasource internal;
}
```
