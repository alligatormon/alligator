# Mtail Configuration

This page documents how to configure the `mtail` module in Alligator.

## Overview

You typically configure mtail in two places:

1. `mtail { ... }` block: registers and compiles mtail scripts
2. `entrypoint { ... }` block: receives input and assigns `handler mtail`

Both can be provided via static config or dynamic API payloads.

## `mtail` Context

The `mtail` context defines a named script program.

### Required Fields

- `name` - unique context key
- `script` - path to the mtail script file

### Optional Fields

- `key` - optional user key (stored with context)
- `log_level_parser`
- `log_level_lexer`
- `log_level_generator`
- `log_level_compiler`
- `log_level_vm`
- `log_level_pcre`

All `log_level_*` fields accept regular Alligator log-level names.

### Example

```
mtail {
    name nginx_mtail;
    script /etc/alligator/mtail/nginx.mtail;
    log_level_vm debug;
}
```

## Entrypoint Integration

To process incoming payloads with mtail:

- set `handler mtail`
- set `mtail <name>` to bind this entrypoint to a named mtail program

### Minimal Example

```
entrypoint {
    bind 0.0.0.0:19101;
    handler mtail;
    mtail nginx_mtail;
}
```

### Useful Optional Entrypoint Fields

Commonly useful with mtail pipelines:

- `log_level` - parser/log verbosity for entrypoint runtime
- `namespace` - prepend metrics namespace
- `env` - pass environment values used in request generation
- `allow` / `deny` - network ACLs
- `tls_certificate` / `tls_key` / `tls_ca` - TLS listener settings
- `mtail_full_export_interval` - how often to run a full mtail variable export for **metric TTL refresh** (see below)

## `mtail_full_export_interval`

After each ingest chunk, Alligator exports mtail VM variables to metrics. For performance it usually exports only variables **touched** in that chunk. Idle label series still need an occasional **full** export so `metric_add` runs for every variable and refreshes the expire-tree TTL (same idea as global `ttl`).

This parameter sets the **minimum wall-clock interval in seconds** between those full exports.

### Behaviour

- **Default:** if the option is omitted or set to an invalid value, the interval is **10 seconds** (same as omitting the field in JSON / plain config).
- **Valid range:** `1` … `86400000` seconds (values outside this range are ignored; default applies).
- **Accepted forms** (same as entrypoint `ttl`):
  - JSON **integer** or **real** (seconds).
  - JSON **string** with human duration units, for example `"120s"`, `"2m"`, `"1h"` (parsed via `get_sec_from_human_range`).

### Where to set it

| Source | How |
|--------|-----|
| Plain **entrypoint** | `mtail_full_export_interval 120;` (numeric token, seconds) |
| Plain **aggregate** | `mtail_full_export_interval=120` inside the aggregate line (same `key=value` style as `ttl=`) |
| API **entrypoint** JSON | `"mtail_full_export_interval": 120` or `"mtail_full_export_interval": "2m"` |
| API **aggregate** JSON | same key on the aggregate object passed to `context_arg_json_fill` |

The value is stored on the parser **`context_arg`** for that entrypoint or aggregate, so each stream can use its own interval.

### Plain entrypoint example

```
entrypoint {
    udp 0.0.0.0:5140;
    handler mtail;
    mtail myscript;
    mtail_full_export_interval 120;
}
```

### API entrypoint example

```
{
  "entrypoint": [
    {
      "udp": ["0.0.0.0:5140"],
      "handler": "mtail",
      "mtail": "myscript",
      "mtail_full_export_interval": "2m"
    }
  ]
}
```

### Aggregate example (plain)

```
aggregate {
    mtail udp://127.0.0.1:5140 mtail_full_export_interval=90 name=myscript;
}
```

## API Configuration

Mtail contexts are supported by the v1 API payload under `mtail`.

### Create or Update

```
{
  "mtail": [
    {
      "name": "nginx_mtail",
      "script": "/etc/alligator/mtail/nginx.mtail",
      "log_level_vm": "debug"
    }
  ]
}
```

### Delete

Delete uses the same shape, identified by `name`.

```
{
  "mtail": [
    {
      "name": "nginx_mtail"
    }
  ]
}
```

With HTTP `DELETE`, Alligator calls the internal mtail context deletion path.

## Notes and Behavior

- If a context with the same `name` already exists, it is replaced.
- If script file cannot be read, context creation fails.
- If no usable compiled context is available for an mtail handler, parser status is marked as failed for that payload.
- Entrypoint-level `mtail` value is stored in parser context name and used for lookup.

## Recommended Layout

For maintainability:

- store scripts in a dedicated directory (for example, `/etc/alligator/mtail/`)
- use stable context names (`service_parser`, `pipeline_parser`, etc.)
- keep one logical script per mtail context

## Configuration Examples

### Example 1: Aggregate file source with mtail script context

```
aggregate {
    mtail file:///var/log/maillog name=postfix log_level=info notify=only state=stream;
}

mtail {
    name postfix;
    script /etc/alligator/mtail/postfix.mtail;
}
```

Use this when Alligator reads logs from a file and parses them with an mtail program selected by `name=postfix`.

### Example 2: Multiple scripts and dedicated ports

```
mtail {
    name nginx_access;
    script /etc/alligator/mtail/nginx_access.mtail;
}

mtail {
    name app_errors;
    script /etc/alligator/mtail/app_errors.mtail;
    log_level_vm debug;
}

entrypoint {
    bind 0.0.0.0:19101;
    handler mtail;
    mtail nginx_access;
    namespace web;
}

entrypoint {
    bind 0.0.0.0:19102;
    handler mtail;
    mtail app_errors;
    namespace app;
}
```

Use this pattern when each log stream has a different grammar and should be parsed by a separate mtail program.

### Example 3: Secure mtail ingestion endpoint

```
mtail {
    name secure_logs;
    script /etc/alligator/mtail/secure_logs.mtail;
}

entrypoint {
    bind 0.0.0.0:19443;
    handler mtail;
    mtail secure_logs;
    tls_certificate /etc/alligator/tls/server.crt;
    tls_key /etc/alligator/tls/server.key;
    tls_ca /etc/alligator/tls/ca.crt;
    allow 10.10.0.0/16;
    deny 0.0.0.0/0;
}
```

Use this for production ingest where only trusted clients should be able to send logs.

### Example 4: Dynamic API registration and entrypoint update

Create/update mtail contexts:

```
POST /api/v1/config
Content-Type: application/json

{
  "mtail": [
    {
      "name": "nginx_access",
      "script": "/etc/alligator/mtail/nginx_access.mtail"
    },
    {
      "name": "app_errors",
      "script": "/etc/alligator/mtail/app_errors.mtail",
      "log_level_vm": "debug"
    }
  ]
}
```

Configure entrypoints that reference them:

```
POST /api/v1/config
Content-Type: application/json

{
  "entrypoint": [
    {
      "bind": "0.0.0.0:19101",
      "handler": "mtail",
      "mtail": "nginx_access",
      "namespace": "web"
    },
    {
      "bind": "0.0.0.0:19102",
      "handler": "mtail",
      "mtail": "app_errors",
      "namespace": "app"
    }
  ]
}
```
