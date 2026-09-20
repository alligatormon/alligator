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
    add_label probe:http;
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

There are **no built-in modules**. `http_2xx` in the example only works after you define `probe { name http_2xx; … }`. A missing `module` returns HTTP 400 (`no such module '…'`).

- `module` — `name` from a `probe` block (required)
- `target` — host, host:port, or path suffix appended to the module scheme (required)
- `hostname` — optional Host header and TLS SNI (use with IP-literal `target`)
- `X-Prometheus-Scrape-Timeout-Seconds` — optional request header; applied only as the **kicked** carg timeout (minus 500 ms, capped by module `timeout`). It does **not** make `/probe` wait.

The handler builds a full URL (`http://`, `https://`, `tcp://`, `icmp://`, `ws://`, `unix://`, …) from `prober`, optional `tls on`, and `target`, then kicks the matching aggregator **asynchronously** (`smart_aggregator` + `aggregator_oneshot_start`) and immediately returns last-known Prometheus text metrics for `host=<target>`. The HTTP response does not wait for the probe to finish (`aggregator_oneshot_await` is never used here). The first scrape can be empty; the next scrape sees this kick’s result. Kick keys are `probe:<module>:<base>` so they do not replace a scheduled `aggregate { blackbox }` of the same host.

## Fields

| Field | Description |
|-------|-------------|
| `name` | Module name referenced by `module=` query parameter (required) |
| `prober` | `http`, `tcp`, `udp`, `icmp`, `dns`, `websocket` (`ws`), or `unix` (required) |
| `tls` | `on` — HTTPS (`prober http` → `https://`), TLS (`prober tcp` → `tls://`), WSS (`prober websocket` → `wss://`), or `tls://unix:` (`prober unix`) |
| `timeout` | Probe timeout applied to the async work (default `5s`) |
| `method` | HTTP method: `GET` (default), `POST`, `HEAD`, `PUT`, `DELETE`, `PATCH` |
| `body` | HTTP request body (inline). Mutually exclusive with `body_file` |
| `body_file` | Path to a file read as the HTTP request body on each `/probe` kick (blackbox_exporter-compatible). If both `body` and `body_file` are set, `body_file` is used |
| `url` | Nameserver URL for `prober dns` (`udp://8.8.8.8:53`, `resolver://`, …) or unix scheme override (`http://unix:`, `unixgram://`) |
| `type` | DNS query type for `prober dns` (`a`, `aaaa`, …; default `a`) |
| `query_name` | If set, blackbox-compatible DNS mode: `target` is the **nameserver**, `query_name` is the name to resolve. Default Alligator mode is the opposite (`target` = name, `url` = server) |
| `valid_rcodes` | Allowed DNS RCODEs (`NOERROR`, `NXDOMAIN`, `0`, …). Default: `NOERROR` only |
| `fail_if_answer_matches_regexp` / `fail_if_answer_not_matches_regexp` | Fail a DNS probe when the concatenated answer section matches / does not match (PCRE, compiled at module load) |
| `unixgram` | `on` — datagram unix socket (`unixgram://`) instead of stream |
| `follow_redirects` | Max redirects for HTTP(S) |
| `valid_status_codes` | Allowed HTTP status patterns (`2xx`, `3xx`, `101`, …). HTTP modules default to `2xx` |
| `fail_if_body_matches_regexp` / `fail_if_body_not_matches_regexp` | Fail the HTTP probe when the body matches / does not match (PCRE, compiled at module load) |
| `fail_if_header_matches` / `fail_if_header_not_matches` | Header PCRE matchers (`header`, `regexp`, optional `allow_missing`) |
| `fail_if_ssl` / `fail_if_not_ssl` | Fail when TLS was / was not used |
| `fail_if_json_not_exists` / `fail_if_json_matches_regexp` | jansson JSON path validators (`path` / `regexp`) |
| `query_response` | TCP/unix dialog: `expect` (PCRE), `send`, `expect_bytes`, `starttls` |
| `negative_test` | Invert `probe_success` (success if the check failed) |
| `payload_size` / `size` | ICMP echo payload size (bytes) |
| `ttl` | ICMP IP TTL / IPv6 hop limit |
| `tos` | ICMP IP TOS / IPv6 traffic class |
| `interval` | Continuous ICMP: one echo per interval; RTT histogram instead of a burst |
| `source_ip_address` | Bind address for the async client (TCP/UDP and ICMP raw sockets, IPv4 and IPv6) |
| `probe_ip_version` / `preferred_ip_protocol` | `4`/`ip4` or `6`/`ip6` (ICMP address family) |
| `loop` | Repeat probe N times (ICMP packet-loss statistics; UDP blackbox multi-packet send) |
| `percent` | Required success ratio when `loop` is set (0.0–1.0) |
| `payload` | UDP send body and integrity pattern: the reply must contain this byte string or `probe_success=0` |
| `ca_file`, `cert_file`, `key_file`, `server_name` | TLS client settings |
| `tls_verify` | `on` — verify server certificate |
| `http_proxy_url` / `proxy` | HTTP proxy URL for the probe request |
| `env` / `header` | Extra HTTP (and WebSocket upgrade) headers (`env=Header:value`) |
| `add_label` | Labels attached to emitted metrics at ingest; also applied on `/probe` OpenMetrics export. Values may contain `@target@` (full `/probe` target) or `@target.host@` (host without port/path). GCE/K8s discovery labels are not substituted |
| `metricstransform` | Label key/value rewrite at ingest and on `/probe` export |
| `metric_name_transform_pattern` / `metric_name_transform_replacement` | PCRE name rewrite on the `/probe` OpenMetrics response |

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

`loop` writes last-burst gauges `alligator_icmp_replies` / `alligator_icmp_losses` (a healthy `loop 10` stays `10` / `0`). Counters `alligator_icmp_replies_total`, `alligator_icmp_losses_total`, and `alligator_icmp_echoes_total` increase by that burst. Prometheus blackbox_exporter has neither: it sends one echo and uses `probe_success`.

HTTPS POST from a file (read at kick time):

```
probe {
    name http_post_file;
    tls on;
    prober http;
    method POST;
    body_file /etc/alligator/probe-body.json;
    env Content-Type:application/json;
}
```

HTTPS POST:

```
probe {
    name http_post_2xx;
    tls on;
    prober http;
    method POST;
    body '{"ok":true}';
    env X-Token:secret;
}
```

DNS (async resolver aggregator; `target` is the name to resolve):

```
probe {
    name dns_a;
    prober dns;
    url udp://8.8.8.8:53;
    type a;
}

curl 'http://127.0.0.1:1111/probe?module=dns_a&target=example.com'
```

WebSocket / Unix:

```
probe {
    name ws_health;
    prober websocket;
}

probe {
    name unix_sock;
    prober unix;
}

curl 'http://127.0.0.1:1111/probe?module=ws_health&target=api.example.com:8080/health'
curl 'http://127.0.0.1:1111/probe?module=unix_sock&target=/var/run/app.sock'
```

TCP/UDP/unix stream probes emit `probe_success` on connect (or TLS handshake). With `query_response`, success is all dialog steps. ICMP supports IPv6 literals such as `icmp://[::1]` and dual-stack names. `GET /probe` stays async: the first scrape can be empty.

TCP SSH banner (from [`src/tests/system/blackbox/alligator.conf`](../src/tests/system/blackbox/alligator.conf)):

```
probe {
    name ssh_banner;
    prober tcp;
    query_response {
        expect "^SSH-2.0-";
        send "SSH-2.0-blackbox-ssh-check";
    }
}

curl 'http://127.0.0.1:1111/probe?module=ssh_banner&target=github.com:22'
```

DNS blackbox-compatible mode (`target` is the nameserver):

```
probe {
    name dns_google;
    prober dns;
    query_name example.com;
    type a;
    valid_rcodes NOERROR NXDOMAIN;
}

curl 'http://127.0.0.1:1111/probe?module=dns_google&target=8.8.8.8'
```

UDP echo with payload integrity and a 3-packet burst (no in-process echo server; the remote must reply with the payload):

```
probe {
    name udp_echo;
    prober udp;
    payload alligator-probe;
    loop 3;
    percent 0.66;
}

curl 'http://127.0.0.1:1111/probe?module=udp_echo&target=127.0.0.1:7'
```

DNS answer-section regex (fail unless an A record for `example.com` appears):

```
probe {
    name dns_example_a;
    prober dns;
    query_name example.com;
    type a;
    fail_if_answer_not_matches_regexp "example\\.com A ";
}
```

`add_label` target substitution on `/probe` (and on `probe_success` ingest labels):

```
probe {
    name http_target;
    prober http;
    add_label instance:@target@ site:@target.host@;
}

curl 'http://127.0.0.1:1111/probe?module=http_target&target=example.com:443'
```

emits labels `instance="example.com:443"` and `site="example.com"`.

## Probe metrics

Native Alligator names (`alligator_*`, `aggregator_*`, `x509_*`). There is no blackbox_exporter alias layer. Unique `probe_*` families stay where they have no native twin (visible on the **next** scrape of async `GET /probe`):

| Metric | Meaning |
|--------|---------|
| `probe_success` | 1 when the module checks passed (inverted by `negative_test`) |
| `probe_failed_due_to_regex` | Set when a TCP/HTTP regex expect or body matcher failed |
| `probe_expect_info` | TCP `query_response` expect step matched |
| `alligator_http_response_status_code` | HTTP status code |
| `alligator_http_response_body_bytes` | HTTP body size |
| `alligator_session_duration_seconds` | Session stage duration in seconds (`stage=connect|write|read|tls_handshake|tls_write|tls_read|shutdown|total`) |
| `alligator_dns_resolve_duration_seconds` | DNS resolve duration in seconds |
| `alligator_probe_timeout_seconds` | Configured module timeout |
| `alligator_probe_ip_protocol` | 4 or 6 from the resolved socket / getaddrinfo family |
| `x509_cert_not_after` | TLS certificate notAfter (Unix seconds) |
| `alligator_icmp_rtt_seconds` | Last ICMP echo RTT in seconds |
| `alligator_icmp_reply_hop_limit` | TTL/hop-limit of the last echo reply (`type=icmp`, `host`). IPv4 uses the IP header TTL; IPv6 uses `recvmsg` ancillary data (`IPV6_HOPLIMIT`) |
| `alligator_icmp_reply_ratio` / `alligator_icmp_loss_ratio` | Last-burst reply/loss ratio in **0–1** (not percent) |

Continuous ICMP (`interval` on `probe` or `aggregate { blackbox icmp://… interval=… }`) emits a classic histogram `alligator_icmp_response_duration_seconds_{bucket,sum,count}`.

HTTP probes also observe `alligator_http_request_duration_seconds_*`. Out of scope: gRPC, HTTP/2, HTTP/3, CEL, libjq, OAuth2.

## JSON configuration

See the `probe` array in [api.md](api.md). Export live config with:

```
curl -s http://127.0.0.1:1111/conf
```

## Related docs

- [blackbox parser](parsers/blackbox.md) — scheduled aggregate checks
- [aggregate.md](aggregate.md) — TLS, proxy, and revocation options on blackbox URLs
- [api.md](api.md) — HTTP endpoints
