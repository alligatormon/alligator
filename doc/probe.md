# Probe modules

**Language / Язык:** [English](probe.md) | [Русский](ru/probe.md)

The `probe` context defines reusable blackbox check modules. They are invoked on demand through `GET /probe` on any HTTP entrypoint, similar to Prometheus Blackbox Exporter.

Periodic checks still use `aggregate { blackbox … }`. Use `probe` when an external system (Prometheus, Alertmanager, custom scripts) chooses targets at request time.

## Overview

```
entrypoint {
    handler prometheus;
    tcp 1111;
}

probe {
    name http_2xx;
    prober http;
    follow_redirects 5;
    valid_status_codes 2xx;
}

probe {
    name icmp;
    prober icmp;
    timeout 5s;
    loop 10;
    percent 0.5;
}
```

Request:

```
curl 'http://127.0.0.1:1111/probe?module=http_2xx&target=example.com'
```

- `module` — `name` from a `probe` block (required)
- `target` — host, host:port, or path suffix appended to the module scheme (required)

The handler builds a full URL (`http://`, `https://`, `tcp://`, `icmp://`, …) from `prober`, optional `tls on`, and `target`, then runs the blackbox parser once and returns Prometheus text metrics.

## Fields

| Field | Description |
|-------|-------------|
| `name` | Module name referenced by `module=` query parameter (required) |
| `prober` | `http`, `tcp`, `udp`, or `icmp` (required) |
| `tls` | `on` — HTTPS (`prober http` → `https://`) or TLS (`prober tcp` → `tls://`) |
| `timeout` | Probe timeout (default `5s`) |
| `method` | `GET` or `POST` (HTTP/HTTPS only) |
| `follow_redirects` | Max redirects for HTTP(S) |
| `valid_status_codes` | Allowed HTTP status patterns (`2xx`, `3xx`, `101`, …) |
| `loop` | Repeat probe N times (ICMP packet loss statistics) |
| `percent` | Required success ratio when `loop` is set (0.0–1.0) |
| `ca_file`, `cert_file`, `key_file`, `server_name` | TLS client settings |
| `tls_verify` | `on` — verify server certificate |
| `http_proxy_url` / `proxy` | HTTP proxy URL for the probe request |
| `env` | Extra HTTP headers (`env=Header:value`) |
| `add_label` | Labels attached to emitted metrics |

## Examples

ICMP with packet loss threshold (from [`src/tests/system/blackbox/alligator.conf`](../src/tests/system/blackbox/alligator.conf)):

```
probe {
    name icmp;
    prober icmp;
    timeout 5000;
    loop 10;
    percent 0.5;
}
```

HTTPS POST:

```
probe {
    name http_post_2xx;
    tls on;
    prober http;
    method POST;
}
```

## JSON configuration

See the `probe` array in [api.md](api.md). Export live config with:

```
curl -s http://127.0.0.1:1111/conf
```

## Related docs

- [blackbox parser](parsers/blackbox.md) — scheduled aggregate checks
- [aggregate.md](aggregate.md) — TLS, proxy, and revocation options on blackbox URLs
- [api.md](api.md) — HTTP endpoints
