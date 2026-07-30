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
