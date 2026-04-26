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
