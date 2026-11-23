**Language / Язык:** [English](varnish.md) | [Русский](../ru/parsers/varnish.md)

## Varnish

Runs `varnishstat -j` via `exec://` and exposes JSON counter values.

### Connection URL

```
exec:///usr/bin/varnishstat -j
```

The command and `-j` (JSON) flag must appear in the URL path after `exec://`.

### Example

```
aggregate {
    varnish exec:///usr/bin/varnishstat -j;
}
```

See [`src/tests/system/varnish/alligator.json`](../../src/tests/system/varnish/alligator.json).

### Requirements

- `varnishstat` installed (Varnish Cache package)
- Permission to read shared memory statistics

### Metrics

Each varnishstat key becomes `varnish_<key>`. Counters (`flag=c`) get a `_total` suffix and `TYPE counter`; gauges/bitmaps stay gauges. HELP uses the varnishstat description when present.

Examples:

- `varnish_cache_hit_total`, `varnish_backend_req_total`
- `varnish_n_object`, `varnish_threads` (gauges)
- `varnish_*_bodybytes_total`
