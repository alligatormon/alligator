**Language / Язык:** [English](haproxy.md) | [Русский](../ru/parsers/haproxy.md)

## HAproxy

Collects HAProxy stats over the admin socket (`show stat`, `show info`, pools, sessions).

### Connection URL

```
tcp://host[:port]
unix:///path/to/haproxy.sock
```

### Example

Over TCP:

```
aggregate {
    haproxy tcp://localhost:9999;
}
```

Over unix socket:

```
aggregate {
    haproxy unix:///var/run/haproxy.sock;
}
```

### Metrics

CSV `show stat` columns and `show info` key/value pairs become `haproxy_*` metrics (frontend/backend/server rows and process info).
