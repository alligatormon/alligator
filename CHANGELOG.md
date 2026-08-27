Changelog

## [unreleased]
- NATS parser: stop dumping every JSON field as a metric. String fields (`tls_version`, `lang`, `uptime`, …) are no longer gauges with value `1`; `/connz` exports aggregates only (no per-`cid` series); `http_req_stats` uses `nats_varz_http_req_stats{endpoint=…}`. Identity strings use `{value="…"} 1`.
- `json_query`: fields listed in `pquery` `[…]` label blocks are labels only and are not emitted as separate series.

## [1.15.2] - 23.08.2026
- TCP and UDP aggregator clients can use an HTTP or SOCKS5 proxy (`proxy=http://…`, `proxy=socks5://…` / `socks5h://`). HTTPS/TLS/TCP go through HTTP CONNECT; plaintext HTTP uses an absolute-URI request; UDP uses SOCKS5 UDP ASSOCIATE only. TLS-to-proxy (`https://proxy`) is not supported.
- OCSP fetches can use a separate proxy (`tls_ocsp_proxy=` on aggregate/entrypoint, `ocsp_proxy=` on x509). Same URL schemes as scrape `proxy=`; scrape `proxy=` is not inherited.
- Config dump no longer crashes when an aggregate has no `url` (`aggregator_generate_conf`).
- Grok matching now uses PCRE 8 instead of Oniguruma. Drop Conan dep `oniguruma`. Capture names are sanitized to PCRE identifiers (`[process][name]` → `process_name`). Existing `%{PATTERN:field}` templates stay valid.
- `aggregator_oneshot_await()`: sequential oneshot on the same aggregator client (TCP/TLS/unix/exec/…) using the nested `uv_run` wait in `events/future.c`. Callback `aggregator_oneshot()` / `try_again` stay for fire-and-forget and parallel fan-out.
- OCSP fetch uses `aggregator_oneshot_await` instead of the cleartext-only uva HTTP client, so `https://` responders work. The unused `src/uva` TCP/HTTP/UDP/Unix copies are removed.
- Cluster k8s peer refresh and Redis glob `KEYS`→`MGET` use oneshot await (serial follow-up). Parallel parser fan-out still uses `try_again`.
- `aggregator_oneshot()` starts the transport immediately (TCP/TLS, unix, UDP, exec, file, PostgreSQL, MySQL, Cassandra). IPv4 literals skip DNS; hostname oneshots connect as soon as the A record lands instead of waiting for the crawl timer.
- TLS/x509: CRL (local file) and OCSP (AIA or `tls_ocsp_responder`) revocation checks for TCP client, mTLS entrypoint, and the filesystem x509 collector. Config: `tls_verify`, `tls_crl`, `tls_ocsp*`, `tls_revocation_mode`, `tls_ocsp_fetch`, `tls_verify_client`. Metrics: `x509_cert_valid{reason="revoked"}`, `x509_cert_revocation_status`, `x509_cert_ocsp_next_update`, `ocsp_requests_total`.
- VRL host: enrichment tables (`enrichment_table` config) with `get_enrichment_table_record` / `find_enrichment_table_records` (CSV `file` + MaxMind `mmdb`/`geoip`). Required Conan dep `libmaxminddb/1.12.2`.
- VRL host: `get_secret` / `set_secret` / `remove_secret` / `set_semantic_meaning` (per-event maps on the VRL stream).
- Fix: TCP/TLS connect failure and empty-body TCP timeout now invoke the oneshot parser handler (so `http_request` records `failure` instead of hanging until VRL timeout when the peer refuses, e.g. mock not listening).
- Fix: TCP aggregator oneshots no longer stick with `lock=1` on DNS miss (hostname `http_request` / HTTPS Referer fetches timed out forever). VRL resume poll retries connect after the A record lands.
- VRL: `http_request` metrics count every served call (including cache hits), not only unique URL fetches.
- VRL: add host builtin `http_request(url)` → `{body, status}` with async suspend/resume (same model as `dns_lookup`). Used to fetch Referer URLs from Apache logs and `parse_json` the body.
- VRL: opt-in negative DNS cache for `dns_lookup` / `reverse_dns` (`dns_negative_ttl` / `dns_negative_ttl_ms`, `dns_negative_cache_max`). Duration fields accept human units (`2s`, `2000ms`, …). Documented in `doc/vrl/README.md`.
- Remove embedded lang interpreters (lua, mruby, duktape). `lang` supports only shared libraries (`lang so` + `modules`). Drop Conan deps `lua` and `duktape`, and the `src/external/mruby` submodule.

## [1.14.0] - 07.07.2021
- Add run commands (actions run when query return not empty) https://github.com/alligatormon/alligator/blob/1.14/src/tests/system/action/alligator.conf
- Add support debian 9, 10
- Add support blackbox interface /probe https://github.com/alligatormon/alligator/blob/1.14/src/tests/system/blackbox/alligator.conf
- Add support DRBD
- Add support NFS
- Add support MoouseFS https://github.com/alligatormon/alligator/blob/1.14/src/tests/mock/moosefs/alligator.conf
- Add support MogileFS
- Update async mode in icmp module
- Add support query by params
- Add support json interface for internal query:
```
curl localhost:1111/json?query='uptime'
[
  {
    "labels": [
      {
        "name": "__name__",
        "value": "uptime"
      }
    ],
    "samples": [
      {
        "timestamp": 2147368803,
        "value": 321,
        "expire": 1625645297
      }
    ]
  }
]
```
- Update metric `socket_counters`: add process and address
```
socket_counters {state="LISTEN", proto="tcp", process="alligator", addr="0.0.0.0"} 1
socket_counters {state="TIME_WAIT", proto="tcp", process="", addr="127.0.0.1"} 1
```

## [1.13.1] - 19.05.2021
- Fix bug with process match

## [1.13.0] - 18.05.2021
- Add support memcached query https://github.com/alligatormon/alligator/blob/master/src/tests/system/memcached/alligator.conf
- Add support redis query https://github.com/alligatormon/alligator/blob/master/src/tests/system/redis/alligator.conf
- Add support oracle query https://github.com/alligatormon/alligator/blob/master/src/tests/mock/oracle/alligator.conf
- Add support clickhouse query
- Add support druid query https://github.com/alligatormon/alligator/blob/master/src/tests/mock/druid/alligator.conf
- Add support couchdb
- Add support couchbase
- Migratete repos to packagecloud

## [1.12.2] - 01.04.2021
- Fix in query filter matching internal metrics

## [1.12.1] - 23.03.2021
- Fix match process with regexp

## [1.12.0] - 23.03.2021
- Support environment variables
- Support scrape kubectl certificates information about expires
- Start migration to conan C/C++ package manager
- Fix memory leak when use graphite/statsd metrics with mapping

## [1.11.3] - 12.03.2021
- Fix support prometheus metrics name with ':' symbol

## [1.11.2] - 03.03.2021
- Fix bugs in filecollector: state "save" did not work.
- Fix memory leak for non-debian distros with enabled "system packages" stats collecting.

## [1.11.1] - 25.02.2021
- Fix bugs in parsers: Flower, Clickhouse, Haproxy, RabbitMQ, Redis (cluster stats), Nginx upstream checks
- Update ACL mechanism for access entrypoints: allow, deny for each entrypoint
- Fix bugs in receivers: pushgateway, statsd and graphite
- Support Rsyslog, Syslog-ng scrape
- Support scrape DNS services: Bind, Unbound, Nsd
- Aerospike aggregator doesn't require description of namespaces (detecting automatically)
- IPMI support by ipmitool
- TFTP active checks
- Support LigHTTPD, Apache HTTPD, Varnish
- Support Squid
- OpenSSL changed to MbedTLS
- Support MySQL and it's stack: Manticoresearch, Sphinxsearch, Proxysql by user SQL queries
- Support PostgreSQL and it's stack: patroni, pgbouncer, odyssey, pgpool by user SQL queries
- Support Cadvisor metrics for Podman, Docker, OpenVZ7, LXC, systemd-nspawn, FreeBSD Jail
- Support Kubernetes scrape endpoints and ingresses
- Support scrape X509 PEM certs from FS or from HTTP/TCP URL, JKS certs support is experimental
- Experimental support for MongoDB, JMX scrape
- Support service discovering/dynamic configuration from Etcd, Consul, Zookeeper and K8S (Zookeeper support only in linux)
- Support UDP, TCP, TLS, HTTP, HTTPS, unix-socket(UDP/TCP) blackbox checking
- HTTP/HTTPS requests now support HTTP headers
- Spawn process now support pass Environment variables
- Support scrape metrics from file
- Support file-stat module, murmur3 hash and crc32 for file checksum
- Update linux scrape: support scrape hardware info, rlimit stats by process, PRM(Centos 7 only) and deb-packages
- Support iptables metrics
- Configuration may be written in JSON, Yaml or classic plain conf file
- Support API for manipulating aggregation targets
- Support custom labels for each target
- Experimental support of internal languages: java
- Basic json support for deserialize to metrics their
- Support internal queries with basic promql syntax (analog alligator-level record rule)
- Support S.M.A.R.T (Linux only)
