## Blackbox in aggregate

To enable the collection of blackbox statistics, use the following option:
```
aggregate {
    #HTTP checks:
    blackbox  http://example.com;
    #ICMP checks:
    blackbox icmp://example.com;
    #BASH exec shell:
    blackbox exec:///bin/curl http://example.com:1111/metrics;

    # Blackbox checks
    blackbox tcp://google.com:80 add_label=url:google.com;
    blackbox tls://www.amazon.com:443 add_label=url:www.amazon.com;
    blackbox udp://8.8.8.8:53;
    blackbox http://yandex.ru;
    blackbox https://nova.rambler.ru/search 'env=User-agent:googlebot';

    # file stat calc:
    blackbox file:///etc/ checksum=murmur3 file_stat=true calc_lines=true;

    # WebSocket connectivity check (handshake + stream):
    blackbox ws://api.example.com:8080/health  add_label=service:api;
    blackbox wss://ws.example.com/status       add_label=service:ws-gateway;

    # gRPC health (grpc.health.v1.Health/Check), not a TCP connect check.
    # service= selects the gRPC service; omit it for a server-wide check.
    # Default port is 80 for grpc:// and 443 for grpcs://.
    blackbox grpc://127.0.0.1:50051;
    blackbox grpc://api.example.com:50051 service=foo.Bar;
    blackbox grpcs://api.example.com:443 service=foo.Bar tls_verify=on tls_server_name=api.example.com;
}
```

ICMP `loop` (or aggregate `pingloop`) overwrites last-burst gauges `alligator_icmp_replies` / `alligator_icmp_losses`. Counters `alligator_icmp_replies_total`, `alligator_icmp_losses_total`, and `alligator_icmp_echoes_total` accumulate across bursts. Prometheus blackbox_exporter sends one echo and has no packet counters.

Continuous ICMP (smokeping_prober-style) uses `interval` instead of a burst:

```
aggregate {
    blackbox icmp://8.8.8.8 interval=1000;
}
```

That emits `alligator_icmp_response_duration_seconds_{bucket,sum,count}`. Optional `payload_size`, `ttl`, `tos`, `negative_test` are accepted on the same aggregate object. On-demand checks stay on **`probe`** + async `GET /probe` (see [probe.md](../probe.md)).

`grpc://` and `grpcs://` run a unary `grpc.health.v1.Health/Check`. `probe_success` is 1 only when the gRPC status is 0 and the health status is `SERVING`. A completed TCP connect is not success. `service=` selects the gRPC service name (protobuf field 1); without it the check is server-wide. `grpcs://` turns TLS on (default port 443). `grpc://` stays plaintext (default port 80).

### Blackbox in entrypoint

On-demand checks use **`probe`** modules and `GET /probe?module=…&target=…` instead of scheduled `aggregate` scrapes. See [probe.md](../probe.md).

Example:

```
entrypoint {
    handler prometheus;
    tcp 1111;
}

probe {
    name http_2xx;
    prober http;
    valid_status_codes 2xx;
}
```

```
curl 'http://127.0.0.1:1111/probe?module=http_2xx&target=example.com'
```

`http_2xx` is not built in: define the `probe` block first, or `/probe` returns HTTP 400.
