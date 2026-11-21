# Blackbox checks

**Language / Язык:** [English](blackbox.md) | [Русский](ru/blackbox.md)

Alligator can run blackbox-style connectivity checks in two ways. Field lists stay in [probe](probe.md) and [blackbox parser](parsers/blackbox.md). This page is copy-paste configs for both modes.

| Mode | Config | How targets are chosen |
|------|--------|------------------------|
| **On demand** | `entrypoint` + `probe { … }` | Prometheus (or a script) calls `GET /probe?module=…&target=…` |
| **Scheduled** | `entrypoint` + `aggregate { blackbox … }` | Fixed URL list on `aggregate_period` |

There are **no built-in probe modules**. A missing `module` on `/probe` returns HTTP 400. `/probe` kicks the check asynchronously and returns the last known metrics; the first scrape for a new target is often empty. Scheduled `aggregate` checks write into the same series store and appear on `GET /metrics`.

## On demand (`GET /probe`)

Save as `/etc/alligator.conf` (or pass with `-c`) and restart Alligator. Port `9115` matches the usual blackbox_exporter listen port.

```
entrypoint {
    handler prometheus;
    tcp 9115;
}

probe {
    name http_2xx;
    prober http;
    valid_status_codes 2xx;
    follow_redirects 5;
    timeout 5s;
}

probe {
    name https_2xx;
    prober http;
    tls on;
    valid_status_codes 2xx;
    follow_redirects 5;
    timeout 5s;
}

probe {
    name icmp;
    prober icmp;
    namespace probes;
    timeout 5s;
    loop 10;
    percent 0.5;
}

probe {
    name dns_a;
    prober dns;
    url udp://8.8.8.8:53;
    type a;
}

probe {
    name ssh_banner;
    prober tcp;
    query_response {
        expect "^SSH-2.0-";
        send "SSH-2.0-blackbox-ssh-check";
    }
}
```

`target` is a host or `host:port`. The scheme comes from `prober` and optional `tls on` — not from the target string. Use `tls on` with `prober http` for HTTPS.

```
curl -sG 'http://127.0.0.1:9115/probe' \
  --data-urlencode 'module=http_2xx' \
  --data-urlencode 'target=example.com'

curl -sG 'http://127.0.0.1:9115/probe' \
  --data-urlencode 'module=https_2xx' \
  --data-urlencode 'target=example.com'

curl -sG 'http://127.0.0.1:9115/probe' \
  --data-urlencode 'module=icmp' \
  --data-urlencode 'target=8.8.8.8'

curl -sG 'http://127.0.0.1:9115/probe' \
  --data-urlencode 'module=dns_a' \
  --data-urlencode 'target=example.com'

curl -sG 'http://127.0.0.1:9115/probe' \
  --data-urlencode 'module=ssh_banner' \
  --data-urlencode 'target=github.com:22'
```

More modules (POST body, gRPC health, DNS `query_name`, UDP payload, header matchers): [probe](probe.md). System-test modules: [`src/tests/system/blackbox/alligator.conf`](../src/tests/system/blackbox/alligator.conf).

## Scheduled (`aggregate { blackbox … }`)

Use this when the URL list is fixed in the config. Prometheus scrapes `GET /metrics` on the entrypoint; it does not call `/probe`.

```
entrypoint {
    handler prometheus;
    tcp 1111;
}

aggregate_period 10s;
aggregate {
    blackbox http://example.com add_label=url:example.com;
    blackbox https://example.com add_label=url:example.com;
    blackbox tcp://google.com:80 add_label=url:google.com;
    blackbox tls://www.amazon.com:443 add_label=url:www.amazon.com;
    blackbox udp://8.8.8.8:53;
    blackbox icmp://8.8.8.8;
}
```

```
curl -s 'http://127.0.0.1:1111/metrics'
```

Schemes include `http`, `https`, `tcp`, `tls`, `udp`, `icmp`, `ws`/`wss`, `grpc`/`grpcs`, `file`, `exec`, and unix sockets. ICMP `loop` / continuous `interval`, WebSocket, and gRPC health examples: [blackbox parser](parsers/blackbox.md). TLS, proxy, and common aggregate options: [aggregate](aggregate.md).

## Related docs

- [probe](probe.md) — `probe` fields and `/probe` metrics
- [blackbox parser](parsers/blackbox.md) — scheduled `blackbox` URLs and schemes
- [receiving metrics](metric-receive.md) — `/probe` in the broader ingest walkthrough
- [api](api.md) — HTTP endpoints including `/probe`
