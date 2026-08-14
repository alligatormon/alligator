# Developer logging guide

Alligator diagnostics go through the functions in `src/common/logs.h` / `src/common/logs.c`. Do not use `printf` / `puts` / `perror` / `fprintf(stderr)` for operational logs, and do not wrap log calls in `if (log_level …)`.

Operator-facing destination and channel configuration is documented in [configuration.md](configuration.md) (log levels, `log_dest`, `log_channel`, raw/out sinks).

## APIs

| Function | When to use |
|----------|-------------|
| `glog(priority, fmt, …)` | Global / no `context_arg` (startup, config parser, modules without a carg) |
| `carglog(carg, priority, fmt, …)` | Anything with a `context_arg *` (aggregates, entrypoints, probes, parsers) |
| `carg_or_glog(carg, priority, fmt, …)` | Null-safe: behaves like `carglog` when `carg` is set, else like `glog` |
| `langlog(lo, priority, fmt, …)` | Lang module only |
| `cslog(priority, fmt, …)` | Chrome CDP module only |

Shipping user/transformed data is separate: `carglog_raw`, `carg_emit_log`, `carg_emit_log_document` (see configuration.md). Do not use those for diagnostics.

Gating: a message is emitted when configured `level >= priority`. Context `carg->log_level == 0` means inherit global `ac->log_level`.

## Levels (`L_*`)

| Priority | Use for |
|----------|---------|
| `L_FATAL` | Unrecoverable startup abort (rare) |
| `L_ERROR` | Failures that stop or break the current operation (connect, parse fatal, TLS, auth) |
| `L_WARN` | Degraded / skipped / unexpected but continuing |
| `L_INFO` | Lifecycle milestones (config chosen, scrape start/end, SD add/remove) — not per-metric |
| `L_DEBUG` | Per-request or parser step without payloads |
| `L_TRACE` | Field-level detail; prefer key, sizes, status codes, truncated preview — not full bodies |

## Rules

1. Prefer `carglog` when a `context_arg *` exists; otherwise `glog`.
2. End every format string with `\n`. Include `carg->key`, host, `errno` / `strerror`, or `uv_strerror` when useful.
3. Do **not** write `if (ac->log_level > N) printf(...)`. Call `glog` / `carglog` and let them gate.
4. Keep an outer level check only when it gates expensive work or non-log behavior (for example dumping amtail variables, or inheriting Chrome stderr).
5. Do not confuse config serialization (`if (log_level)` in `config/get.c` meaning “emit the field”) with diagnostic logging.
6. Leave CLI `--help` / usage text and test-harness stdout alone.

## Before / after

```c
/* before — bypasses channels, formats, and L_* semantics */
if (carg->log_level > 0)
	printf("no arg 'module' in query '%s'\n", http_data->uri);

/* after */
carglog(carg, L_WARN, "no arg 'module' in query '%s'\n", http_data->uri);
```

```c
/* before */
if ((s = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
	if (carg->log_level > 0)
		perror("socket");
	return;
}

/* after */
if ((s = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
	carglog(carg, L_ERROR, "unixgram socket(%s): %s\n", carg->key, strerror(errno));
	return;
}
```

## Anti-patterns

- Manual `if (log_level > N)` with `printf` / `puts` / `perror`
- Logging full request/response bodies at `L_INFO` or `L_DEBUG` on hot paths
- Using `glog` when a `carg` is available (loses per-context channel and `[key]` prefix)
- Treating numeric levels as the old `0/1/2/3` scale — always pass `L_*` enums
