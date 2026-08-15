# VRL (avrl) in alligator

avrl is the C reimplementation of Vector Remap Language. In alligator it is
linked as a **separate static library** (`avrl_static`) with glue under `src/vrl/`.

## Source resolution (better than flat-amtail listing)

CMake looks for avrl in this order:

1. `src/external/avrl/` — git submodule (preferred for releases)
2. `../avrl/` — sibling checkout (local development)

Current branch uses git submodule `src/external/avrl`.
`depbuild.sh` rsyncs it to the remote builder (same pattern as amtail).

## Config

```
vrl {
    name app_logs;
    script /etc/vrl/app.vrl;
    # or: program ".status = upcase(.message)";
}

aggregate {
    vrl file:///var/log/app.log name=app_logs
        start_pattern=^\S
        condition_pattern=^\s
        multiline_mode=continue_through
        log_level=info;
}

entrypoint {
    bind 0.0.0.0:19191;
    handler vrl;
    vrl app_logs;
}
```

### Multiline (Vector-compatible, shared with mtail and grok)

Same assembler as Vector file sources (`start_pattern` + `condition_pattern` + `mode`).
Configured on the **aggregate** (applies to `mtail`, `grok`, and `vrl`) or on the
program JSON/`multiline` object.

| Field | Meaning |
|-------|---------|
| `start_pattern` | Regex; a matching line begins a new logical message |
| `condition_pattern` | Regex interpreted according to `multiline_mode` |
| `multiline_mode` | `continue_through` \| `continue_past` \| `halt_before` \| `halt_with` |

Example (Java-style stack traces — lines continuing with whitespace):

```
aggregate {
    mtail file:///var/log/app.log name=app
        start_pattern=^\S
        condition_pattern=^\s
        multiline_mode=continue_through;
}
```

On the `vrl` program (JSON API):

```json
"multiline": {
  "start_pattern": "^\\S",
  "condition_pattern": "^\\s",
  "mode": "continue_through"
}
```

Legacy single-pattern form still works: `{ "mode": "halt_before", "pattern": "^\\S" }`
(uses the pattern for both start and condition).

JSON API key: `"vrl": [ { "name": "...", "script"|"program": "...", "multiline": {...}, "dns_timeout": "2s", "dns_negative_ttl": "30s" } ]`

### Async DNS: `dns_lookup` / `reverse_dns`

> **Host builtins (alligator glue).** These are not part of the avrl library itself;
> alligator registers them in `src/vrl/vrl_dns.c` and drives suspend/resume from
> `src/vrl/vrl.c`. They share alligator's resolver cache (`resolver_cache_lookup`).

Both functions are **fully asynchronous**: resolution runs via libuv
(`uv_getaddrinfo` / `uv_getnameinfo`) and does **not** block other alligator
work (other aggregates, scrapes, entrypoints, timers). While a stream waits,
alligator keeps serving the rest of the process.

On a **cache miss**, the builtin starts that one-shot async resolution, returns
`null`, and pauses **this** stream. `vrl_run_record()` buffers the record and a
poll timer replays it when the answer lands (or after `dns_timeout`, replays
once with `null` via `dns_force_null`). Further records from the same source are
queued until then, so **log lines for that stream are processed only after a
full resolution completes or a (positive/negative) cache hit** — order is
preserved, but throughput for that file can stall on cold lookups.

**`reverse_dns` vs `dns_lookup` in log pipelines:** PTR lookups often take
**much longer in wall-clock terms across a stream** than forward `dns_lookup`.
Client/peer IPs in logs are highly variable (many distinct addresses, low
reuse), so the positive cache helps less and each new IP is a first-sight miss
(suspend + resolve, or a negative-cache window if enabled). Forward lookups of
a small set of hostnames tend to warm the cache quickly. Prefer negative cache
and/or avoid unconditional `reverse_dns` on high-cardinality IP fields when
latency matters.

| Field | Alias | Default | Meaning |
|-------|-------|---------|---------|
| `dns_timeout` | `dns_timeout_ms` | `2s` (2000 ms) | How long a record may wait for a first-sight resolution before the timeout path |
| `dns_poll` | `dns_poll_ms` | `50ms` | How often the resume timer re-checks the cache |
| `dns_negative_ttl` | `dns_negative_ttl_ms` | `0` (off) | Opt-in negative cache TTL (see below) |
| `dns_negative_cache_max` | — | `100000` | Cap on distinct remembered bad names |

**Duration values** use the same rules as other alligator time fields
([configuration.md](../configuration.md#available-units-for-time-data-in-configuration-file)):

- JSON/plain **string** with units: `ms`, `s`, `m`, `h`, `d`, `w` (e.g. `"2s"`, `"2000ms"`, `"30s"`). A bare number in a string is treated as **seconds**.
- JSON **integer** / **real**: milliseconds (so `2000` == `"2000ms"`).
- Plain config always yields strings, so prefer an explicit unit: `dns_timeout 2s;` rather than a bare number.

### Async HTTP: `http_request`

> **Host builtin (alligator glue)** in `src/vrl/vrl_http.c`. Same suspend/resume
> model as DNS: cache miss → oneshot GET → pause stream → replay with result.

```
resp = http_request(.referer)   # null while pending / on failure
# resp.body (string), resp.status (int)
obj, err = parse_json(resp.body)
```

Uses `aggregator_oneshot` under the hood. IP-literal hosts are seeded into the
resolver cache so local URLs like `http://127.0.0.1:8765/...` do not stall.
The pause deadline is at least 10s (or `dns_timeout` if larger). Reuses
`dns_timeout` / `dns_poll` for the await timer.

Timing metrics (system namespace, same idea as DNS) — counted **once per
`http_request()` call that returns a final answer** (warm cache hits included).
Network awaits attribute wall-clock wait; pure cache hits use duration 0:

- `vrl_http_requests_total{result="success|failure|timeout", method="GET"}`
- `vrl_http_request_duration_seconds_sum{result=..., method="GET"}`

Average latency = `duration_sum / requests_total` for a given `result`.
Per-line script metrics (e.g. `apache_referer_json_total`) remain separate.

### Enrichment tables (host)

Declare named tables in config, then call Vector-compatible builtins:

```
enrichment_table {
    name codes;
    type file;                    # CSV (header row required)
    path /etc/alligator/codes.csv;
}

enrichment_table {
    name city;
    type mmdb;                    # or geoip
    path /usr/share/GeoIP/GeoLite2-City.mmdb;
}
```

```
row, err = get_enrichment_table_record("codes", {"code": .code})
rows = find_enrichment_table_records("codes", {"code": .code})
geo, err = get_enrichment_table_record("city", {"ip": .client_ip})
```

- **file**: exact-match on all condition object fields; `get_*` errors if 0 or >1 rows.
- **mmdb/geoip**: condition must include `"ip"`; returns Vector-like fields (`country_code`,
  `city_name`, `latitude`, …) when present. Requires Conan/system `libmaxminddb`
  (hard dependency).

### Secrets and semantic meaning (host)

Per-event maps on the VRL stream (cleared between records):

```
set_secret("token", "s3cr3t")
tok = get_secret("token")
remove_secret("token")
set_semantic_meaning(".status", "http.status_code")   # string path (optional leading '.')
```

#### Negative DNS cache (opt-in)

**Problem it solves:** previously a name that failed or timed out was never
remembered, so every subsequent log line with that bad domain triggered a fresh
async resolution and a full `dns_timeout` suspend of the pipeline. With the
negative cache enabled, failures/timeouts are memoized for a configurable window.

**Lookup order** in `vrl_dns_common()`:

1. `dns_force_null` one-shot (timeout replay) — unchanged
2. Positive cache (`resolver_cache_lookup`) — a real answer always wins
3. Negative cache — if a live entry exists, return `null` immediately (no suspend, no re-resolve)
4. Cache miss → suspend + kick off async resolution

A negative entry is recorded when:

- `getaddrinfo` / `getnameinfo` returns an error or empty result (resolver callbacks in `vrl_dns.c`)
- a paused stream times out waiting (`vrl_resume_timer_cb` in `vrl.c`)

When the entry's TTL lapses, the name is re-resolved. A short TTL re-resolves bad
domains sooner; a longer one retries persistently-bad names less often.

**Plain config:**

```
vrl {
    name postfix;
    script /etc/vrl/postfix.vrl;
    dns_timeout 2s;
    dns_negative_ttl 30s;
    dns_negative_cache_max 50000;
}
```

**JSON API:**

```json
{
  "name": "postfix",
  "script": "/etc/vrl/postfix.vrl",
  "dns_timeout": "2s",
  "dns_negative_ttl": "30s",
  "dns_negative_cache_max": 50000
}
```

Integer milliseconds still work (`"dns_timeout_ms": 2000`, `"dns_negative_ttl_ms": 30000`).

- `dns_negative_ttl` — enable and set the negative TTL. `0` / omitted disables it
  and preserves the old suspend-each-time behavior.
- `dns_negative_cache_max` — max distinct remembered names (default `100000`) to
  bound memory. When full, new bad names fall back to suspend-each-time.

**Implementation notes**

- Process-global `alligator_ht` keyed by `"<name>:<rrtype>"`, storing an absolute
  `expire_ms`.
- Guarded by its own `uv_mutex_t`: search-then-evict/refresh runs on the loop
  thread (callbacks/timer) and during VRL record processing; `alligator_ht`'s
  internal rwlock only protects individual ops, not the compound sequence.
- Expired entries are evicted lazily on access; insertions stop at the cap.
- Positive precedence is preserved: if an async resolution succeeds after a
  timeout, the positive cache entry takes over and any stale negative entry is
  ignored (then eventually evicted).

Metrics (system carg): `vrl_dns_resolutions_total` and
`vrl_dns_resolution_duration_seconds_sum` with labels `type` (`A`/`AAAA`/`PTR`)
and `result` (`success`/`failure`/`timeout`).

## Debugging

On the aggregate line set `log_level=info` (or `debug`):

```
aggregate {
    vrl file:///var/log/app.log name=app log_level=info;
}
```

Also run alligator with `-l 3` (or higher) so global logs show.

You should see lines like:

```
vrl: handler got N bytes (name=app)
vrl: run record (...) program='app': 'hello...'
vrl: metric_add lines_total = 1
vrl: processed chunk via linebuf (multiline=on)
```

If you see **nothing** after appending to the log:

1. Is alligator actually reading the file? Check aggregate is `state=stream` / filetailer path.
2. Prefer **append after start**: `echo hello >> /var/log/app.log`
3. Lines need a trailing `\n`.
4. Program must be registered under the same `name=` as the aggregate.
5. Prefer a **script file** over inline `program` (quoting in plain config is easy to break):

```
# /etc/vrl/app.vrl
.metric = { "name": "lines_total", "value": 1 }

# alligator.conf
vrl { name app; script /etc/vrl/app.vrl; }
aggregate { vrl file:///var/log/app.log name=app log_level=info; }
```

With `log_level=debug` you also get the full JSON event after transform.

### Metric export: `.metric` / `.metrics`

> **Deviation from Vector VRL.** Standard Vector Remap Language has no metric
> export objects, no Prometheus histogram observation, and no `update` /
> `buckets` fields. `.metric` / `.metrics` (and the histogram shape below) are
> an **Alligator extension** on top of avrl: the script remaps the event, then
> Alligator reads these fields and pushes series into its metric tree.

Each object needs `name` (string) and `value` (number/bool). Optional:

| Field | Meaning |
|-------|---------|
| `labels` | object of string labels |
| `update` | if `true`, call `metric_update` (increment) instead of `metric_add` (set) |
| `type` | `counter` / `gauge` / `histogram` / `summary` — sets Prometheus `# TYPE` |
| `buckets` | array of numeric upper bounds — **histogram observation** (Alligator-only) |

#### Histogram observation (`buckets`)

When `buckets` is a non-empty number array (and `type` is omitted or
`"histogram"`), Alligator treats `value` as one sample and updates:

- `{name}_bucket{le="…"}` — cumulative counts for every bound `value <= bound`, plus `le="+Inf"`
- `{name}_sum` — adds `value`
- `{name}_count` — adds `1`

```
.metrics = [{
  "name": "postgresql_query_duration_ms",
  "value": .ms,
  "type": "histogram",
  "buckets": [1, 5, 10, 25, 50, 100, 250, 500, 1000, 5000],
  "labels": {
    "database": .database_name,
    "duration_kind": .duration_kind
  }
}]
```

This is the same idea as mtail `histogram … buckets …`, not a Vector VRL feature.
You can still emit raw `_bucket` / `_sum` / `_count` series yourself with
`"update": true` if you need full control.

### Log export: `.log` / `.logs` → `log_channel_out`

> **Deviation from Vector VRL.** Shipping remapped events to a sink is not part
> of standard VRL. Alligator only emits logs when the script sets **explicit**
> `.log` or `.logs`, and the aggregate/entrypoint has `log_channel_out` set.
> Metrics (`.metrics`) and logs are independent — both can fire on the same record.

| Field | Meaning |
|-------|---------|
| `.log` | one string (plain body) or one **flat object** (JSON document) |
| `.logs` | array of strings and/or flat objects |

Objects are written as **flat** JSON documents (not nested under `message`).
Strings go through `log_channel_write_raw` (json/elastic channels wrap them as `message`).

```
.log = {
  "database": .database_name,
  "query": .query,
  "application_name": .application_name,
  "message": .pg_message
}
.metrics = [{ "name": "postgresql_log_lines_total", "value": 1, "update": true, "labels": { "database": .database_name } }]
```

```
aggregate {
    vrl file:///var/log/pg.csv name=postgresql_csv
        log_channel_out=pg-json
        start_pattern='^\d{4}-\d{2}-\d{2}'
        condition_pattern='^\s'
        multiline_mode=continue_through;
}
```

Without `.log` / `.logs`, nothing is sent to `log_channel_out` (metrics-only).
See also `log_channel_raw` for untransformed passthrough.

## Glob file sources (PostgreSQL csvlog example)

Basename globs work on `file://` aggregates (see [aggregate.md](../aggregate.md#glob--match-basename-filter)):

```
vrl {
    name postgresql_csv;
    script /etc/vrl/postgresql_csv.vrl;
}

aggregate {
    vrl file:///spool/postgres-logs/pg/postgresql-2026-08-*.csv name=postgresql_csv
        state=stream
        start_pattern='^\d{4}-\d{2}-\d{2}'
        condition_pattern='^\s'
        multiline_mode=continue_through
        log_level=info;
}
```

Quote patterns that contain `{` / `}` — otherwise the plain tokenizer treats `{` as a context brace and truncates (e.g. `^\d{4}` → `^\d`).

Equivalent:

```
aggregate {
    vrl file:///spool/postgres-logs/pg/ match=postgresql-2026-08-*.csv name=postgresql_csv
        state=stream
        start_pattern='^\d{4}-\d{2}-\d{2}'
        condition_pattern='^\s'
        multiline_mode=continue_through;
}
```

