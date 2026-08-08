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
    script /etc/alligator/vrl/app.vrl;
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

JSON API key: `"vrl": [ { "name": "...", "script"|"program": "...", "multiline": {...} } ]`

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
# /etc/alligator/vrl/app.vrl
.metric = { "name": "lines_total", "value": 1 }

# alligator.conf
vrl { name app; script /etc/alligator/vrl/app.vrl; }
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

## Glob file sources (PostgreSQL csvlog example)

Basename globs work on `file://` aggregates (see [aggregate.md](../aggregate.md#glob--match-basename-filter)):

```
vrl {
    name postgresql_csv;
    script /etc/alligator/vrl/postgresql_csv.vrl;
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

