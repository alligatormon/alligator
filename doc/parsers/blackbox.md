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
}
```

ICMP `loop` (or aggregate `pingloop`) overwrites last-burst gauges `aggregator_packet_received` / `aggregator_packet_loss`. Counters `aggregator_packet_received_total`, `aggregator_packet_loss_total`, and `aggregator_packet_sent_total` accumulate across bursts. Prometheus blackbox_exporter sends one echo and has no packet counters.

Continuous ICMP (smokeping_prober-style) uses `interval` instead of a burst:

```
aggregate {
    blackbox icmp://8.8.8.8 interval=1000;
}
```

That emits `alligator_icmp_response_duration_seconds_{bucket,sum,count}`. Optional `payload_size`, `ttl`, `tos`, `negative_test` are accepted on the same aggregate object. On-demand checks stay on **`probe`** + async `GET /probe` (see [probe.md](../probe.md)).

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
