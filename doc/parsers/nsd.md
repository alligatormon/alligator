**Language / Язык:** [English](nsd.md) | [Русский](../ru/parsers/nsd.md)

## NSD

Reads NSD statistics from the control socket.

### Connection URL

```
unix:///run/nsd/nsd.ctl
```

### Example

```
aggregate {
    nsd unix:///run/nsd/nsd.ctl;
}
```

See [`src/tests/system/nsd/alligator.json`](../../src/tests/system/nsd/alligator.json).

### Protocol

Alligator sends `NSDCT1 stats_noreset\n` on the control socket and parses counter lines.

### Metrics

Examples:

- `nsd_queries{type="…"}`
- `nsd_num*`, `nsd_num_{class,opcode,rcode}*`
