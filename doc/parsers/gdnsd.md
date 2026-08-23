**Language / Язык:** [English](gdnsd.md) | [Русский](../ru/parsers/gdnsd.md)

## gdnsd

Reads gdnsd statistics from the control socket as JSON.

### Connection URL

```
unix:///path/to/gdnsd/control.sock
tcp://unix:/path/to/gdnsd/control.sock
```

The parser sends an 8-byte `S` control header and parses the JSON response.

### Example

```
aggregate {
    gdnsd unix:///usr/local/var/run/gdnsd/control.sock;
}
```

See [`src/tests/system/gdnsd/alligator.yaml`](../../src/tests/system/gdnsd/alligator.yaml).

### Options

Optional `pquery` / `jpath` on the aggregate line filter JSON fields (see [json_query.md](json_query.md)).

### Metrics

Examples: `gdnsd_stats_noerror`, `gdnsd_udp_*`, `gdnsd_tcp_*` (prefix `gdnsd_`).
