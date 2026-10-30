# Entrypoint
The entrypoint is necessary to enable ports that allows Prometheus pull daemons to gather information.
Alligator configuration supports a list of entrypoints, providing the capability to have many different ports for various purposes.

<br>
<p align="center">
<h1 align="center" style="border-bottom: none">
    <img alt="alligator-cluster-entrypoint" src="images/entrypoint.jpeg"></a><br>
</h1>
<br>
<br>
</p>

Entrypoint functionality includes:
- Push data interface
- Metric return
- Alligator API
- Replication log for [cluster](https://github.com/alligatormon/alligator/blob/master/doc/cluster.md) configuration.

Walkthrough for StatsD, Graphite, blackbox `/probe`, cluster gauges, auth, and `metric_filter`: [receiving metrics](metric-receive.md).

Full explain
```
entrypoint {
    log_level <time>;
    log_channel <channel_name>;
    log_channel_raw <channel_name>;
    return [empty|on]>;
    auth <basic|bearer|other> <user:password|token|secret>;
    auth_header <header_name>;
    satisfy [any|all];
    authorize {
        secret <master>;
        period <seconds>;
        set [cookie|header|json];
        validate [cookie|header];
        cookie_name <name>;
        header_name <name>;
        bind remote_addr;
        bind header <name>;
        bind cookie <name>;
    }
    limit {
        path <path>;
        rps <requests per second>;
        key remote_addr;
        key header <name>;
        key cookie <name>;
    }
    header <name> <value>;
    ttl <time to live>;
    read_metric_interval <seconds>;
    tcp <port>;
    tcp <addr>:<port>;
    tls <port>;
    tls <addr>:<port>;
    udp <port>;
    udp <addr>:<port>;
    unixgram <path/to/socket>;
    unix <path/to/socket>;
    allow <CIDR>;
    deny <CIDR>;
    api [on|off];
    tls_certificate <path>;
    tls_key <path>;
    tls_ca <path>;
    tls_verify [on|off];
    tls_verify_client [off|optional|require];
    tls_crl <path>;
    tls_crl_scope [leaf|chain];
    tls_ocsp [on|off];
    tls_ocsp_responder <http://url>;
    tls_ocsp_proxy <http://proxy:port>;
    tls_ocsp_timeout <time>;
    tls_ocsp_cache_ttl <time>;
    tls_revocation_mode [soft|hard];
    tls_ocsp_fetch [background|inline];
    handler <handler>;
    pingloop <number>;
    metric_aggregation [off|count]; # for counting histograms and counter datatypes as aggregation gateway
    format [openmetrics|prometheus]; # metrics exposition format for prometheus handler (default: openmetrics)
    add_label <name>:<value>;
    metric_name_transform_pattern <pcre>;
    metric_name_transform_replacement <string>;
    metricstransform { ... };
    metric_filter {
        include <name|regex> match_type strict|regexp;
        exclude <name|regex> match_type strict|regexp;
        include_label <label> [<value>] match_type strict|regexp;
        exclude_label <label> [<value>] match_type strict|regexp;
    };
    metric_include <name|regex> match_type strict|regexp;
    metric_exclude <name|regex> match_type strict|regexp;
    metric_include_label <label> [<value>] match_type strict|regexp;
    metric_exclude_label <label> [<value>] match_type strict|regexp;
    # Prometheus histograms: TYPE histogram on base name (ut_hist) or on components
    # (ut_hist_bucket / _sum / _count) are merged into one family on scrape/export
    cluster <cluster_name>;
    instance <instance_name>;
    threads <number of threads>;
    lang <lang>;
    mapping {
        template <template>;
        name <name>;
        label <label_name> <label_value>;
        buckets <buckets 1> <buckets 2> ... <buckets N>;
        le <le 1> <le 2> ... <le N>;
        quantiles <quantile 1> <quantile 2> ... <quantile N>;
        percentile_buffer_min <N>;
        percentile_calc_every <N>;
        quantile_window <seconds>;
        quantile_window_empty delete|zero;
        match [glob];
    }
}
```

## log\_level
Default: off\
Plural: no\
Specify of the level of logging for this context. Units for this option are explained in this [document](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-log-levels)

```
entrypoint {
    log_level debug;
    tcp 1111;
}
```

## return
Default: on\
Plural: no\
Possible values:
- on
- empty

Enabling or disabling the response body on requests. This can be useful for Alligator ports that need to be accessed from the internet as the solution for receving Pushgateway metrics from a browser's JavaScript.

## auth
Default: -\
Plural: yes

Possible values:
- `basic <user>:<password>`
- `bearer <token>`
- `other <secret>`

"auth" option allows for HTTP authentication methods to access this port.

There is support for three types of HTTP authentication: Basic, Bearer and simple secret. Basic authentication enables the use of a [login/password](https://datatracker.ietf.org/doc/html/rfc7617) for HTTP authentication, while Bearer authentication allows for the use of statically specified [tokens](https://datatracker.ietf.org/doc/html/rfc6750).
The "other" type of authentication is custom method specific to Alligator, which only allows for the use of statically specified secrets in the configuration file and compares the client passed header for validation.

Alligator responds with a 401 HTTP status code if authentication data is not specified, and it responds with a 403 HTTP status code if authentication fails.

The configuration allows for combination of several methods within one context. For instance, the following configuration enables authentication through any of following header:
```
entrypoint {
    auth basic root:qwerty;
    auth basic user:password;
    auth bearer ZV0aFdP2kx44WVWRSkFCQsDhKvAHuA6Hhw4Kzr6OhoGe13RKxyUjgZo7ODco34sq;
    auth bearer F89rMiV1h8YyVhMIk9rI1GLSxW3fquSHCjf1PuAReABa47ivUbjshmTVZkD5bpXs;
    auth other LUTTK4SrdH;
    auth other RiLJXWD3Xu;
    tcp 1111;
}
```

Example of authorizations on Alligator:
```
$ curl -w '%{response_code}\n' -u nouser:nopassword http://localhost:1111
403
$ curl -w '%{response_code}\n' -u root:qwerty http://localhost:1111
200
$ curl -w '%{response_code}\n' -H "Authorization: Bearer ZV0aFdP2kx44WVWRSkFCQsDhKvAHuA6Hhw4Kzr6OhoGe13RKxyUjgZo7ODco34sq" http://localhost:1111
200
$
```

## auth_header
Default: Authorization\
Plural: no

This option allows for the change of the authorization header name to other.

## satisfy
Default: any\
Plural: no

Possible values:
- `any`
- `all`

`satisfy` combines the static auth group (`auth basic`, `auth bearer`, `auth other` — any one of them is enough) with the time-based authorize secret. It does not change `allow` / `deny`.

- `any` — the request is accepted when either group succeeds. This is the default when both groups are configured.
- `all` — both groups must succeed.

When only one group is configured, that group is required and `satisfy` is not consulted. `OPTIONS` still skips authentication. `GET /authorize` skips only the time-based secret and still requires static auth when static auth is configured.

Missing credentials are HTTP 401. Credentials that are present but do not match are HTTP 403.

## authorize
Default: off\
Plural: no

Issues a time-based secret from `GET /authorize` and requires that secret on other requests. With no `bind`, the code is HMAC-SHA256 of `master_secret || ":" || decimal_time_bucket` (the HMAC key is the master secret), written as 64 lowercase hex characters. `period` is the bucket size in seconds (`10`, `10s`, `1m`, …). A request is accepted when the presented code matches the current bucket or one bucket on either side.

The master secret and the issued code are not written to the log.

`GET /authorize` does not itself require a valid code, so a client can obtain the first one. Static `auth` still applies to that request. With only `authorize` configured, minting does not need HTTP authentication (IP `allow` / `deny` are unchanged).

`set` chooses the mint response:
- `cookie` (default) — `200` and `Set-Cookie: <cookie_name>=<hex>; Path=/; Max-Age=<period>; HttpOnly`
- `header` — `200` and a response header `<header_name>: <hex>`
- `json` — `200`, `Content-Type: application/json`, body `{"secret":"<hex>","period":<P>,"expires":<unix>}` where `expires` is the unix time at the end of the current bucket

`validate` chooses where clients send the code back: `cookie` (default) or `header`. `set json` with `validate header` is valid: the mint response is JSON, and later requests carry the code in the header. The authorize header is separate from `Authorization`, so basic auth and the time-based secret can be required together (`satisfy all`).

`bind` mixes the client into that HMAC message, in the order written. Repeat it. At most 8 binds. The HMAC key stays the master secret. Each value is length-prefixed, so the current bucket and every configured bind have to be present and identical on `GET /authorize` and on the later request.

- `bind remote_addr` — accepted TCP peer, without the port. An IPv4-mapped IPv6 address is stored as IPv4. A unix socket has no peer address.
- `bind header <name>` — that request header. The name is not `header_name`.
- `bind cookie <name>` — that cookie. The name is not `cookie_name`.

A missing or repeated header or cookie on `GET /authorize` is HTTP 400. A missing peer is HTTP 500. On later requests a code that does not match these values is HTTP 403. The same header or cookie sent twice on one request does not match. A header or cookie the client chooses only separates clients who send different values. Several `bind header` or `bind cookie` lines are all required; each name is distinct. A bind entry that is rejected (unknown type, missing or extra name, or the code header/cookie name) denies every request on that entrypoint.

```
entrypoint {
    auth basic user:pass;
    satisfy any;
    authorize {
        secret change-me;
        period 10s;
        set cookie;
        validate cookie;
        cookie_name alligator_auth;
        header_name X-Alligator-Auth;
        bind remote_addr;
        bind header X-Client;
        bind cookie sid;
    }
    tcp 9091;
}
```

```
$ curl -sD- -u user:pass -H 'X-Client: scraper-a' --cookie sid=sess-1 http://localhost:9091/authorize
$ curl -s -u user:pass -H 'X-Client: scraper-a' --cookie 'sid=sess-1; alligator_auth=<hex>' http://localhost:9091/metrics
```

## limit
Default: off\
Plural: yes

Caps HTTP requests to one exact path. The query string is ignored, so `path /authorize` matches `/authorize` and `/authorize?x=1`, and does not match `/authorized` or `/authorize/`. Every method counts, including a failed authentication and `OPTIONS`. The check runs before `GET /authorize` mints a code.

`rps` is an integer number of requests per second, at least 1. After a pause a client may send `rps` requests at once, then one per `1/rps` seconds. A request over the limit is HTTP 429 with `Retry-After`.

A `limit` block that cannot be installed (missing `path`, `rps` below 1, an unknown `key`, or no memory for the table) makes every request on that entrypoint HTTP 500. The path is not left unlimited.

With no `key` lines, every client on the entrypoint shares one bucket. Each `key` line is part of the same bucket id, in the order written. A request must pass every `limit` block whose path matches, so a second block is a separate budget.

- `key remote_addr` — accepted TCP peer, without the port
- `key header <name>` — that request header
- `key cookie <name>` — that cookie

A missing or empty value shares one bucket. It is not exempt. Header and cookie names use the same character rules as authorize `bind` names. A keyed rule keeps at most 4096 live buckets. A bucket with no request for 60 seconds may be reused. A new value while the table is full is HTTP 429, so a header or cookie the client chooses cannot mint an unlimited number of budgets. A full table does not drop an existing bucket to make room. When the key is chosen by the client, add a second `limit` on the same path with no `key`. That shared bucket still applies after the table is full. `remote_addr` is the accepted TCP peer, not `X-Forwarded-For`.

Listener `threads` share one table, so `threads 4` does not turn 10 rps into 40. Different ports are separate entrypoints.

```
entrypoint {
    authorize { secret change-me; period 30s; }
    limit { path /authorize; rps 100; }
    limit {
        path /authorize;
        rps 10;
        key remote_addr;
        key header X-Client;
        key cookie sid;
    }
    tcp 9091;
}
```

## header
Default: -\
Plural: yes

The header option makes it possible to respond with custom headers to clients. Custom headers work only if authorization is passed, or the request method is OPTIONS.

An example of its usage is privided below:

```
entrypoint {
    header access-control-allow-headers 'Authorization';
    header access-control-allow-methods POST;
    header access-control-allow-origin *;
    tcp 1111;
}
```

## ttl
Default: 300\
Plural: no\
Possible values:
- 0
- {any_number}{any_unit}


This options allows user to set maximum time to live metrics that have been pushed by statsd, pushgateway or graphite methods.
TTL in alligator provides capability to remove obsolete metrics, that haven't been updated for a long time.

A zero value can be used to disable the TTL functionality. This means that any received metrics are never deleted;

The following configuration demonstrates the use of TTL. Metrics that haven't been updated for one hour will be deleted.
```
entrypoint {
    ttl 1h;
    tcp 1111;
}
```

To disable the TTL functionality:
```
entrypoint {
    ttl 0;
    tcp 1111;
}
```

Another way to specify a TTL for each pushed request is to use the X-Expire-Time header. For instance, the following command creates a metric with a TTL of 1 day:
```
curl -sS -d "restore_test 1" -H "X-Expire-Time: 1d" 127.0.0.1:1111
```

More information about units that user can specify in configuration can be obtained from configuration [doc](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md).

## read_metric_interval
Default: 10 (seconds)\
Plural: no

Minimum wall-clock interval between updates to `alligator_session_reads_total` and `alligator_session_read_bytes_total` for stream entrypoints (TCP, TLS, and Unix stream servers) while connections are active, and for UDP entrypoints **without** metric aggregation lock. Counters still increase on every read or datagram; this option only limits how often those series are written to the metric tree. Omitted or zero in JSON uses the default of 10 seconds. UDP listeners with lock continue to flush through `aggregator_events_metric_add` on each datagram as before.

Plain configuration accepts an integer number of seconds. JSON entrypoints also accept a string or real value parsed like other duration fields (see `mtail_full_export_interval`).


```
entrypoint {
    read_metric_interval 30;
    tcp 1111;
    udp 1111;
}
```


# tcp, tls, udp, unix, unixgram
Default: -\
Plural: yes

Configuration specifies the listen port (or socket) for income queries. This is central part of entrypoint context. Any entrypoint must have at least one of this option to run entrypoint.
Alligator usually runs on port tcp to response with metrics to Prometheus.
UDP configuration is usually can be used for statsd metrics because udp is one of supported protocol for statsd.
UDP and unix socket also can be used to obtain rsyslog stats in [impstats](https://github.com/alligatormon/alligator/blob/master/doc/parsers/rsyslog.md).

For example, the following configuration enables the TCP and UDP ports to receive StatsD metrics in TCP and UDP mode:
```
entrypoint {
    tcp 1111;
    udp 1111;
}
```

In the next example, alligator opens two ports on INADDR_ANY and another one on the localhost address:
```
entrypoint {
    tcp 1111 80 127.0.0.1:9000;
}
```

An additional example demonstrating the use of a Unix-socket is as follows:
```
entrypoint {
        unix /tmp/alligator.sock;
}

system {
        base;
}
```

To test this, use the following command:
```
$ curl --unix-socket /tmp/alligator.sock localhost/metrics
context_switches_total 339451
cpu_cores 1
cpu_cores_cgroup 0
cpu_cores_hw 1
cpu_usage_calc_delta_seconds 18446744073.591579
cpu_usage_time {type="idle"} 2053.710000
cpu_usage_time {type="iowait"} 16.770000
cpu_usage_time {type="nice"} 0.000000
cpu_usage_time {type="system"} 21.850000
cpu_usage_time {type="user"} 83.900000
forks_total 4625
interrupts_total 135704
softirq 180882
time_seconds 1724141426
$
```

# allow, deny
Default: -\
Plural: yes\
Possible values:
- {IPv4 address}
- {IPv6 address}
- {IPv4 CIDR}
- {IPv6 CIDR}

These options control access policies for Alligator's port restrictions.
One important thing to note is that this option has a default policy. If first argument in the configuration is "allow", then the default policy will be "deny". Conversely, if the first argument is "deny", it will allow permission to anyone not listed in the "deny" lists.

The following configuration is an example of allowing access to port 80 only from localhost, allowing access to port 1111 from private networks, and implementing a more complex access policy for port 1112:
```
entrypoint {
    tcp 80;
    allow 127.0.0.1;
}

entrypoint {
    tcp 1111;
    allow 192.168.0.0/24;
    allow 172.16.0.0/12;
    allow 10.0.0.0/8;
}

entrypoint {
    tcp 1112;
    deny 100.64.0.0/10;
    deny 10.15.20.30;
    deny 127.0.0.2;
    allow 10.0.0.0/8;
}
```

## api
Default: off\
Plural: no\
Possible values:
- on
- off

Enables or disables the ability to make PUT/POST request to the Alligator API for updating the runtime configuration. The API documentation is available in the [document](https://github.com/alligatormon/alligator/blob/master/doc/api.md)


## tls\_certificate
Default: -\
Plural: no

Specifies the path of the server x509 certificate.

## tls\_key
Default: -\
Plural: no

Specifies the path of the server x509 key.

## tls\_ca
Default: -\
Plural: no

CA bundle used to verify client certificates (mTLS) and to validate OCSP responses.

## tls\_verify / tls\_verify\_client
`tls_verify on` enables peer verification. `tls_verify_client require` additionally sets `SSL_VERIFY_FAIL_IF_NO_PEER_CERT`. `optional` requests a client cert without requiring one.

## tls\_crl / tls\_ocsp
Same revocation keys as aggregate (`tls_crl`, `tls_crl_scope`, `tls_ocsp`, `tls_ocsp_responder`, `tls_ocsp_proxy`, `tls_ocsp_timeout`, `tls_ocsp_cache_ttl`, `tls_revocation_mode`, `tls_ocsp_fetch`). Used for mTLS client certificates.

## handler
Default: prometheus\
Plural: no\
Possible values:
- prometheus
- rsyslog-impstats
- auditd
- lang
- log
- grok
- mtail

This option specifies the handler that processes received messages.

- **prometheus** (default) — Prometheus pull, pushgateway, statsd, and graphite push queries.
- **rsyslog-impstats** — Rsyslog impstats metrics. See [rsyslog](https://github.com/alligatormon/alligator/blob/master/doc/parsers/rsyslog.md).
- **auditd** — Linux audit log lines in `key=value` format. See [auditd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/auditd.md).
- **lang** — Custom processing via external lang modules. Requires the `lang` option. See [lang](https://github.com/alligatormon/alligator/blob/master/doc/lang.md).
- **log** — Log shipping only: no metric parsing. Incoming bytes are forwarded via `log_channel_raw` (see [configuration — raw stream passthrough](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#raw-stream-passthrough-log_channel_raw)). Pair with `log_channel_raw` on file/socket transports.
- **grok** — Parses the stream with a named [grok](https://github.com/alligatormon/alligator/blob/master/doc/grok.md) context. Requires the `grok` option.
- **mtail** — Parses the stream with a named [mtail](https://github.com/alligatormon/alligator/blob/master/doc/mtail/README.md) program. Requires the `mtail` option.


## grok
Default: -\
Plural: no

Name of the grok context to use when `handler grok` is set. Grok contexts are defined in the top-level [grok](https://github.com/alligatormon/alligator/blob/master/doc/grok.md) block.


## mtail
Default: -\
Plural: no

Name of the mtail program to use when `handler mtail` is set. Programs are defined in the top-level [mtail](https://github.com/alligatormon/alligator/blob/master/doc/mtail/configuration.md) block.


## mtail_full_export_interval
Default: 60s\
Plural: no

Minimum interval between full exports of all mtail variables when using `handler mtail`. See [mtail configuration](https://github.com/alligatormon/alligator/blob/master/doc/mtail/configuration.md#mtail_full_export_interval).


## namespace
Default: -\
Plural: no

Assigns metrics from this entrypoint to a named namespace. See [namespace](https://github.com/alligatormon/alligator/blob/master/doc/namespace.md).


## http_keepalive
Default: on (JSON entrypoints)\
Plural: no

Enables HTTP keep-alive for stream entrypoints that speak HTTP.


## http_idle_timeout
Default: 75s\
Plural: no

Idle timeout for HTTP keep-alive connections on stream entrypoints.


## lang
Default: -\
Plural: no

The [lang](https://github.com/alligatormon/alligator/blob/master/doc/lang.md) option specifies the context that operates the received body with custom functions via external modules.


## pingloop
Default: 0\
Plural: no\
Possible values:
- {number}

Pingloop allows a blackbox handler to ping resource more than once. Last-burst gauges `alligator_icmp_replies` / `alligator_icmp_losses` are overwritten each run; `alligator_icmp_replies_total`, `alligator_icmp_losses_total`, and `alligator_icmp_echoes_total` accumulate.


## metric_aggregation
Default: off\
Plural: no\
Possible values:
- off
- count

This option makes it possible to count counters and histogram metrics inside Alligator instead of replacing them when a metric is pushed with pushgateway protocol. It opens the prom-aggregation-gateway interface. Graphite and statsd cannot specify this as they already support counting metrics inside protocol.


## pushgateway body formats
Push requests to `/metrics/job/<job>/instance/<instance>` accept two bodies:

- **Text** (default): Prometheus or OpenMetrics text, including a missing `Content-Type`, `text/plain`, and `application/openmetrics-text`.
- **Delimited protobuf**: `Content-Type: application/vnd.google.protobuf; proto=io.prometheus.client.MetricFamily; encoding=delimited` (or the `application/x-protobuf` alias). `COUNTER`, `GAUGE`, `UNTYPED`, `SUMMARY`, and `HISTOGRAM` families are converted to text exposition before ingest; `GAUGE_HISTOGRAM` is skipped with a warning. Like the Prometheus text encoder, a histogram without an explicit `+Inf` bucket gets `le="+Inf"` added from `sample_count` (or from the last bucket when `sample_count` is absent).

`encoding=text` and `encoding=compact-text` return `415 Unsupported Media Type`. Malformed delimited framing, or a non-empty protobuf body that yields no series, returns `400 Bad Request`. Successful and empty protobuf bodies return `202 Accepted`. URL grouping labels merge with body labels; when the same label name appears in both, the URL value wins.

### pushgateway DELETE
`DELETE /metrics/job/<job>[/<label>/<value>...]` removes pushed series and returns `202 Accepted`.

Alligator does not keep Pushgateway grouping-key registries, so DELETE is a **label-selector delete**: every series in the entrypoint namespace whose label set contains **all** URL grouping labels is removed, whatever its metric name. Deleting a selector that matches nothing is a no-op and still returns `202`.

This differs from upstream Pushgateway, which deletes only the exact grouping key. Here `DELETE /metrics/job/x` also removes series that were pushed under `/metrics/job/x/instance/y`, and it removes any series that carries `job="x"`, however it got there (not only pushes).

| Request | Status |
|---------|--------|
| URL with `/metrics/` and a `job` label | `202 Accepted` (also when nothing matched) |
| URL with `/metrics/` but no `job` label (`/metrics/`, `/metrics/instance/a`), or an odd number of path components (`/metrics/job/a/instance`) | `400 Bad Request` |
| Path without `/metrics/` (outside `/api`) | `404 Not Found` |
| Out of memory while collecting or deleting | `500 Internal Server Error` (series collected before the failure are still removed) |

Not supported: `PUT` replace-group semantics and a wipe-all admin call.


## format
Default: openmetrics\
Plural: no\
Possible values:
- openmetrics
- prometheus

Selects the text exposition format returned by the **prometheus** handler on GET scrape requests. By default Alligator serves [OpenMetrics](https://openmetrics.io/) text (`Content-Type: application/openmetrics-text`) with a trailing `# EOF` line. Set `format prometheus` to use the classic Prometheus text format instead (`Content-Type: text/plain`).

Query parameters still override this setting for individual requests: `?format=prometheus`, `?format=openmetrics`, `?openmetrics=0`, or `?openmetrics=1`.

Example for Prometheus-compatible scrapers that expect the legacy format:

```
entrypoint {
    format prometheus;
    tcp 9100;
}
```


## cluster
Default: -\
Plural: no

Specify the cluster name for receiving metrics. More information about cluster can be found in the [cluster](https://github.com/alligatormon/alligator/blob/master/doc/cluster.md) documentation.

## instance
Default: -\
Plural: no\

Specify the cluster current instance name for receiving metrics. More information about cluster can be found in [cluster](https://github.com/alligatormon/alligator/blob/master/doc/cluster.md) documentation.


## threads
Default: 0\
Plural: no\

Specifies the number of threads working in this entrypoint.

## add_labels
Default: -\
Plural: yes

Use `add_label <name>:<value>;` inside `entrypoint` to attach labels on this endpoint.

- **Prometheus / OpenMetrics pull** (`GET /`): labels are applied at scrape time on the wire. Stored series are unchanged.
- **Push handlers** (statsd/graphite/pushgateway): the same hash is merged at ingest, so stored series include the labels.

Multiple `add_label` directives are merged into a single object in JSON config output. Collector labels win when a key already exists.

The following configuration is an example of adding labels for metrics received on port 80:
```
entrypoint {
    tcp 80;
    allow 127.0.0.1;
    add_label received:statsd;
    add_label server:alligator;
}
```

## metric_filter
Default: - (accept all)\
Plural: yes (`metric_include` / `metric_exclude` / `metric_include_label` / `metric_exclude_label` one-liners)

Filter metrics at ingest (statsd / Graphite / pushgateway / OpenMetrics / OTLP push, and all other `metric_add*` / `metric_update*` paths on that context) **before** `add_label` merge and `metricstransform`. Aggregate scrapes use the same gate when configured on the aggregate item. Matching is **whole-sample**: a matching exclude_label drops the entire sample (including OTLP — previously a rejected label was skipped while the sample continued; that per-label skip is intentionally unified into whole-sample drop).

Semantics (order; excludes win):

1. No `metric_filter` / empty or all-invalid rules → accept all. Invalid PCRE rules are skipped at compile time (WARN); if nothing valid remains, the filter is cleared.
2. **Name include**: if any `include` rules → name must match ≥1.
3. **Label include**: if any `include_label` rules → sample must match ≥1 (label present; if the rule has a value, value must match `strict`/`regexp`; omit value or empty string ≡ any value for that label name).
4. **Name exclude**: if name matches any `exclude` → drop.
5. **Label exclude**: if any label on the sample matches any `exclude_label` rule → drop.
6. `match_type` is `strict` (exact) or `regexp` / `regex` (PCRE), same vocabulary as [`metricstransform`](#metricstransform). Missing `match_type` defaults to `strict`. Unknown values are treated as `strict` (WARN). NULL / empty sample labels → label includes fail if any exist; label excludes are a no-op.

Does not filter system metrics unless a system context is given this option (default: no; `NULL` carg / no filter → allow-all). Outbound / action filtering is out of scope. Legacy entrypoint `reject` is removed: if a JSON `reject` key or plain `reject` keyword is still present it is ignored with a WARN — migrate to `exclude_label`. Pushgateway URI labels are no longer whole-URI aborted on a reject match; filtering is per-sample at `metric_add` / `metric_update`.

**Sharp edge:** an include-only filter on an entrypoint also applies to metrics ingested via that carg, including `alligator_session_*` / other `alligator_*` session metrics. Prefer include patterns that also match those names (or use exclude-only) if you need session metrics retained. There is no automatic `alligator_*` exemption.

**Config order (plain):** the first definition of `metric_filter` on a construct wins.

- A native `metric_filter { ... }` block or JSON argument creates the filter. Later blocks/JSON are ignored (WARN).
- One-liners `metric_include` / `metric_exclude` / `metric_include_label` / `metric_exclude_label` **append** to an existing filter, or **create** one if none exists yet.
- Therefore: if one-liners appear **before** a later `metric_filter { ... }` block, they already created the object and the block is **ignored** (first wins, WARN).

Recommend one of: put the `metric_filter { ... }` block first (optional one-liners after it append), use only one-liners, or use only a block.

Block form:
```
entrypoint {
    tcp 9091;
    metric_filter {
        include ^app_.*$ match_type regexp;
        exclude go_gc_duration_seconds match_type strict;
        exclude_label http_response_code 404 match_type strict;
        exclude_label name bot;
        exclude_label role;
        include_label env prod match_type strict;
    };
}
```

One-liners only (create the `metric_filter` object):
```
entrypoint {
    tcp 9091;
    metric_include ^app_.*$ match_type regexp;
    metric_exclude go_gc_duration_seconds match_type strict;
    metric_exclude_label http_response_code 404 match_type strict;
    metric_include_label env prod;
}
```

JSON shape:
```json
"metric_filter": {
  "include": [{"metric": "^app_.*$", "match_type": "regexp"}],
  "exclude": [{"metric": "go_.*", "match_type": "regexp"}],
  "exclude_label": [
    {"label": "http_response_code", "value": "404", "match_type": "strict"},
    {"label": "name", "value": "bot"},
    {"label": "role"}
  ],
  "include_label": [
    {"label": "env", "value": "prod", "match_type": "strict"}
  ]
}
```

## metricstransform
Default: -\
Plural: no

`metricstransform` rewrites **label keys and/or values**.

- **Push handlers**: applied at ingest; rewritten labels are stored.
- **Prometheus / OpenMetrics pull**: applied at scrape time on stored labels (export rewrite). Matching uses the stored metric name **or** the rewritten export name, same dual-name matching as [actions](https://github.com/alligatormon/alligator/blob/master/doc/action.md#matching-metric-names-include-metric-metric-regex).

The value is an OTel-collector-like JSON object (or array) with `transforms`, `operations`, and `value_actions` rules. JSON may also use `new_label` or `label_key_actions` on each operation for key renames (see [action.md § metricstransform](https://github.com/alligatormon/alligator/blob/master/doc/action.md#metricstransform)); the native plain block supports `new_label` but not `label_key_actions` (use JSON for regex-based key edits).

In plain config, provide either a **JSON string** or a **native block** (see [action.md § metricstransform](https://github.com/alligatormon/alligator/blob/master/doc/action.md#metricstransform) for the keyword grammar; it is the same here). A native block can follow other keywords on the same construct without a semicolon before `metricstransform` (same chaining rules as on [actions](https://github.com/alligatormon/alligator/blob/master/doc/action.md#metricstransform) and [aggregates](https://github.com/alligatormon/alligator/blob/master/doc/aggregate.md#metricstransform)).

Example with JSON string (rewrite `instance` to host-only value):
```
entrypoint {
    tcp 1111;
    handler prometheus;
    metricstransform '{"transforms":[{"include":"^.*$","match_type":"regexp","operations":[{"action":"update_label","label":"instance","value_actions":[{"regex":"^([^:]+):?.*$","replacement":"$1"}]}]}]}';
}
```

Same rule as a native block:
```
entrypoint {
    tcp 1111;
    handler prometheus;
    metricstransform {
        include ^.*$ match_type regexp label instance regex '^([^:]+):?.*$' replacement '$1';
    };
}
```

## metric_name_transform_pattern / metric_name_transform_replacement
Default: -\
Plural: no

PCRE rewrite of metric **names on Prometheus/OpenMetrics scrape** (`GET /`). Stored names are not changed (PromQL still uses the original names). `# HELP` / `# TYPE` family headers use the export name; type metadata is looked up from the stored name when the export name has no family record.

Same `$1` replacement semantics as [action](https://github.com/alligatormon/alligator/blob/master/doc/action.md) name transform.

```
entrypoint {
    handler prometheus;
    tcp 1111;
    add_label env:prod;
    metric_name_transform_pattern ^x509_cert_(.*)$;
    metric_name_transform_replacement cert_$1;
    metricstransform {
        include ^.*$ match_type regexp label instance regex '^([^:]+):?.*$' replacement '$1';
    };
}
```

## mapping
Default: -\
Plural: yes

Mapping enables the support of aggregating and relabeling functions for statsd and graphite push protocols.
Firstly, the only supported method to match is 'glob'.

## template
Specified the template.

## name
Specifies the new metric name.

## label
Specifies the new labels.

## match
Specifies options to match and split metric with the template.

## bucket
Enables the separation of input metrics into multiple buckets.

## le
Enables the separation of input metrics into multiple LE buckets.

## quantile

Ring size is derived from quantile precision (`0.9` → 10, `0.90` → 100, `0.900` → 1000, `0.9000` → 10000). Optional per-mapping `percentile_buffer_min` / `percentile_calc_every` override the globals of the same name; when omitted on a mapping, the global values are used. `percentile_buffer_min` raises ring size without changing the `quantile=` label (floor only; finer specs are not shrunk). `percentile_calc_every` limits how often the ring is snapshotted and heapsorted (collect and sort use separate buffers).

`quantile_window` (seconds, or a duration such as `60s`) restricts the quantile to samples newer than that window. `0` or unset keeps the legacy sample-count ring. The effective window is `min(quantile_window, ring_size / insert_rate)`: if inserts wrap the ring first, the window is shorter than configured. Inserts recalculate at most once a second (`percentile_calc_every` is ignored) and the expire sweep refreshes every windowed series, so a sample that ages out drops off within about one 10s tick. `quantile_window_empty` chooses an empty window: `delete` (default) removes the `*_quantile` series on that sweep; `zero` keeps emitting `0` until the source metric itself expires. `zero` is not the default because a latency of 0 reads as "fast". Both settings are accepted globally and per `mapping` (mapping unset → global fallback). An unknown policy or duration is a warning and falls back to `delete` or `0`.

Example of using statsd mapping:
```
entrypoint {
        udp 127.0.0.1:8125;
        tcp 8125;
        mapping {
                template test1.*.test2.*;
                name "$1"_"$2";
                label label_name_"$1" "$2"_key;
                buckets 100 200 300;
                match glob;
        }
        mapping {
                template test2.*.test3.*;
                name "$1"_"$2";
                label label_name_"$1" "$2"_key;
                le 100 200 300;
                match glob;
        }
        mapping {
                template test3.*.test4.*;
                name "$1"_"$2";
                label label_name_"$1" "$2"_key;
                quantiles 0.999 0.95 0.9;
                percentile_buffer_min 1000;
                percentile_calc_every 100;
                match glob;
        }
}
```
