**Language / Язык:** [English](metric-receive.md) | [Русский](ru/metric-receive.md)

# Receiving metrics

Alligator can sit in front of Prometheus as the process that accepts pushes, answers scrapes, and runs on-demand blackbox checks. This page is the walkthrough. Field lists stay in [entrypoint](entrypoint.md), [cluster](cluster.md), and [probe](probe.md).

One process can listen on several ports. HTTP ports share authentication. UDP ports do not: StatsD and Graphite plaintext have no headers, so restrict them with `allow` / `deny` and keep Basic auth and the time-based secret on the HTTP port that Prometheus scrapes.

```
entrypoint {
    handler prometheus;
    tcp 1111;
    format prometheus;
    ttl 5m;
    allow 10.0.0.0/8;
}
```

`GET /` and `GET /metrics` return the series stored on that entrypoint. `ttl` drops StatsD, Graphite, and Pushgateway series that have not been updated. `ttl 0` keeps them until restart.

## Blackbox

Two different jobs use the name blackbox.

**On demand.** Prometheus (or a script) calls `GET /probe`, the same shape as blackbox_exporter. Define a `probe` module first. There are no built-in module names. The handler starts the check and returns the last result immediately; the first call for a new target is often empty.

```
entrypoint {
    handler prometheus;
    tcp 9115;
}

probe {
    name http_2xx;
    prober http;
    valid_status_codes 2xx;
    timeout 5s;
}

probe {
    name icmp;
    prober icmp;
    timeout 5s;
    loop 10;
    percent 0.5;
}
```

```
curl -sG 'http://127.0.0.1:9115/probe' \
  --data-urlencode 'module=http_2xx' \
  --data-urlencode 'target=example.com'
```

`target` is a host or `host:port`. The scheme comes from `prober` and `tls`. Use `tls on` with `prober http` for HTTPS. `prober grpc` runs `grpc.health.v1.Health/Check`; TLS is `tls on` on the module, not a scheme on `target`. Scheduled checks of a fixed URL list belong in `aggregate { blackbox … }`, not on this port. See [probe](probe.md) and [blackbox](parsers/blackbox.md).

## StatsD

StatsD is accepted on the default `handler prometheus` entrypoint, over UDP or TCP. Dots in the name become underscores. `|g` and `|ms` replace the series (a gauge). `|c` adds to it. DogStatsD tags after `#` become labels.

```
entrypoint {
    handler prometheus;
    udp 127.0.0.1:8125;
    tcp 8125;
    ttl 5m;
    allow 10.0.0.0/8;
}
```

```
printf 'app.queue.depth:12|g|#host:web01\n' | nc -u 127.0.0.1 8125
printf 'app.requests:1|c|#host:web01,env:prod\n' | nc 127.0.0.1 8125
```

The stored series are `app_queue_depth{host="web01"}` and `app_requests{host="web01",env="prod"}`. `mapping` rewrites globbed names before they are stored. See [entrypoint — mapping](entrypoint.md#mapping).

## Graphite

Graphite plaintext is the same ingest path: `name value [timestamp]`. The timestamp is ignored. The value replaces the series, same as a gauge. Dots in the path become underscores.

```
entrypoint {
    handler prometheus;
    tcp 2003;
    udp 2003;
    ttl 5m;
    allow 10.0.0.0/8;
    mapping {
        template servers.*.cpu.*;
        name node_cpu;
        label host "$1";
        label mode "$2";
        match glob;
    }
}
```

```
printf 'servers.web01.cpu.user 12.5 %s\n' "$(date +%s)" | nc 127.0.0.1 2003
```

With that mapping the series is `node_cpu{host="web01",mode="user"}`. Without a mapping the name is `servers_web01_cpu_user`: `web02` is another name, not a `host` label. Two agents that send the same path still overwrite one gauge.

Pushgateway text on `POST /metrics/job/...` is the HTTP equivalent. It is covered in [entrypoint](entrypoint.md#pushgateway-body-formats). `metric_aggregation count` sums Pushgateway counters and histograms inside Alligator. StatsD and Graphite already choose add versus replace with `|c` and a bare value, so that option does not apply to them.

## Cluster, gauges, and several hosts

Counters and histograms do not need a cluster: aggregate them in PromQL (`sum`, `rate`, `histogram_quantile`). Gauges do.

When high availability is not required, run one Alligator per server and scrape that host. The gauge stays where it was pushed.

To spread writers across several Alligators without oplog, pin each client to one node with sticky sessions in nginx (`ip_hash` or a sticky cookie).

`DELETE /metrics/job/...` removes series only on the node that receives the request. That holds for a per-host installation and for sticky sessions on the balancer. If the request can land on any node, expire the series with `ttl` instead of DELETE.

`cluster { type oplog }` copies received samples to other Alligator nodes. `sharedlock` is the other type: it makes an `aggregate` crawl run on one node, and it does not replicate pushes.

Each receiver lists the same servers and sets `instance` to its own `host:port` from that list. The string must match.

```
cluster {
    name recv;
    type oplog;
    size 10000;
    sharding_key __name__;
    replica_factor 2;
    servers recv1.example:1111 recv2.example:1111;
}

entrypoint {
    handler prometheus;
    tcp 1111;
    udp 8125;
    cluster recv;
    instance recv1.example:1111;
    ttl 5m;
}
```

`recv2` is the same block with `instance recv2.example:1111`.

`sharding_key __name__` (the default) places every sample of a metric name on one primary. `replica_factor 2` keeps that primary copy and one more node. A gauge is replaced, not added: the latest value for a series wins on the primary, and the replica stores that same value. A StatsD `|c` still increments on the primary. The copy forwarded to another node follows the datatype. Counter and histogram copies are added. Gauge and untyped copies are replaced. StatsD and Graphite samples are untyped unless you opt into `metric_aggregation`, so their replicas replace.

That is why a host label matters. Two agents pushing `queue_depth` with no label are one series, and the later gauge overwrites the earlier one on whichever node owns that name. Push `queue_depth` with `host=web01` and `host=web02` and both series stay. To pin a series by host instead of by name, set `sharding_key host` (the label value is the hash input; a missing label does not add a key).

The primary stores the series in the entrypoint namespace. A replica copy is stored under `<cluster>:<primary-instance>`, for example `recv:recv1.example:1111`. Scrape it with `GET /metrics?namespace=recv:recv1.example:1111`. `GET /oplog` shows the replication log. See [cluster](cluster.md).

## Who may push and scrape

HTTP authentication is per entrypoint. It covers scrape, Pushgateway, `/probe`, and the API. It does not cover UDP.

Static methods are `auth basic`, `auth bearer`, and `auth other`. Any one of them is enough. Missing credentials are 401. Present but wrong credentials are 403.

The time-based secret is a second group. `GET /authorize` returns the current code and does not itself require a previous code. `period` is the bucket size. Clients may present the current bucket or one bucket on either side. `validate cookie` reads the cookie. `validate header` reads `header_name` (default `X-Alligator-Auth`), which is not the `Authorization` header. `bind remote_addr`, `bind header <name>`, and `bind cookie <name>` tie that code to the accepted address, a request header, and a cookie. The same values must be on the mint request and on later requests. See [authorize](entrypoint.md#authorize).

`satisfy` combines the two groups:

| Config | Accept the request when |
|--------|-------------------------|
| `satisfy any` (default) | Basic/bearer/other succeeds, or the time-based code is valid |
| `satisfy all` | Both succeed |

`GET /authorize` always skips only the time-based check. With `satisfy all` and `auth basic`, minting still needs the password.

Basic **or** cookie:

```
entrypoint {
    handler prometheus;
    tcp 1111;
    auth basic metrics:change-me;
    satisfy any;
    authorize {
        secret change-me-too;
        period 30s;
        set cookie;
        validate cookie;
        cookie_name alligator_auth;
    }
}
```

```
curl -sD- -u metrics:change-me http://127.0.0.1:1111/authorize
curl -s -u metrics:change-me http://127.0.0.1:1111/metrics
curl -s --cookie alligator_auth=<hex> http://127.0.0.1:1111/metrics
```

Header instead of a cookie: `set header`, `validate header`, then send `X-Alligator-Auth: <hex>` on the scrape or push. `set json` returns `{"secret","period","expires"}` for a client that does not want `Set-Cookie`. JSON `expires` is the end of the current bucket. The code stays valid through the next bucket as well.

Basic **and** header:

```
entrypoint {
    tcp 1111;
    auth basic metrics:change-me;
    satisfy all;
    authorize {
        secret change-me-too;
        period 30s;
        set json;
        validate header;
        header_name X-Alligator-Auth;
    }
}
```

```
code=$(curl -s -u metrics:change-me http://127.0.0.1:1111/authorize | sed -n 's/.*"secret":"\([0-9a-f]*\)".*/\1/p')
curl -s -u metrics:change-me -H "X-Alligator-Auth: $code" http://127.0.0.1:1111/metrics
```

`allow` / `deny` still run at accept time, before this HTTP check. See [entrypoint — auth](entrypoint.md#auth) and [authorize](entrypoint.md#authorize).

## What to keep

`metric_filter` drops samples at ingest, before `add_label` and `metricstransform`. A label added with `add_label` cannot be used in the filter. Excludes win. If any `include` rule exists, the name must match one of them.

```
entrypoint {
    handler prometheus;
    tcp 1111;
    udp 8125;
    metric_filter {
        include ^app_ match_type regexp;
        include ^node_cpu match_type regexp;
        include ^probe_ match_type regexp;
        exclude_label env test match_type strict;
    }
}
```

An include-only filter also drops `alligator_*` series ingested on that same context, including session metrics. Add an include for the names you still want, or use excludes only. The first `metric_filter` block on the entrypoint wins. Later blocks are ignored. See [entrypoint — metric_filter](entrypoint.md#metric_filter).
