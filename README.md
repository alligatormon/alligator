**Language / Язык:** [English](README.md) | [Русский](README.ru.md)

<h1 align="center" style="border-bottom: none">
    <img width="100" height="100" alt="Alligator" src="doc/images/logo.min.png"><br>Alligator
</h1>

<p align="center">
Alligator is an aggregator for system and software metrics. It is an incredibly versatile tool, allowing anyone to effortlessly gather and aggregate metrics from a wide array of sources, including software, operating systems, and numerous other systems in infrastructure. Its capabilities empower users to comprehensively monitor and analyze the performance and behavior of servers. By seamlessly interfacing with diverse systems and platforms, Alligator enables users to gain visibility and insight into their infrastructure and applications.
<br>
<br>
</p>


# Installation
Alligator supports GNU/Linux and FreeBSD.
For installation instructions, see the [distribution](doc/distribution.md) doc.

For more examples check URL with [tests](https://github.com/alligatormon/alligator/tree/master/src/tests/system)

# Unit tests and coverage
The `src/tests/unit2/` suite is organized by feature headers (for example `netlib.h`, `http.h`, `parsers.h`) and is intended to keep test coverage close to the related production code.

Coverage flow (scope: `src/**/*.c`, excluding `src/tests/**`, `src/external/**`, and `src/build/**`):
```
cd src
./tests/coverage/run_coverage.sh
```

Artifacts:
- `src/tests/coverage/coverage_report.txt` - full `llvm-cov report` output
- `src/tests/coverage/coverage_top15.txt` - 15 lowest-covered C files in scope
- `doc/coverage-baseline.md` - baseline snapshot and threshold ramp

Testing principles are documented in `doc/testing.md`.

To enforce a minimum in local runs:
```
cd src
MIN_LINE_COVERAGE=50 ./tests/coverage/run_coverage.sh
```

> Note: building tests requires project dependencies (for example `jansson`) and initialized external sources/submodules.

# Command line

```
alligator [-h|--help] [-v|--version] [-l <level>] [<path>]
```

| Flag | Description |
|------|-------------|
| `-h`, `--help` | Print usage and exit |
| `-v`, `--version` | Print version and exit |
| `-l <num\|name>` | Override log level (numeric or name: `off`, `fatal`, `error`, `warn`, `info`, `debug`, `trace`) |
| `<path>` | Config file or directory (optional; can repeat) |

If no path is given, the default config basename is `/etc/alligator` on Linux and `/usr/local/etc/alligator` on FreeBSD.

# Configuration description:
Alligator supports YAML, JSON or plain-text format. In examples we will consider only plain text format. For more information please refer to the detailed documentation or the tests.

When no path is passed on the command line, alligator loads the default basename (`/etc/alligator` on Linux, `/usr/local/etc/alligator` on FreeBSD). For that basename it tries, in order:

1. `{basename}.json`
2. `{basename}.yaml`
3. `{basename}.conf`

If `{basename}` is a directory, every file inside it with extensions `.yaml`, `.json`, or `.conf` is loaded (split configs / includes). If `{basename}` is a regular file, that file is parsed directly.

You can also pass an explicit path on the command line:
```
alligator /path/to/alligator.conf
alligator /etc/alligator.d/
```

Configuration can be overridden or extended with environment variables (`ALLIGATOR__…`). See [configuration — environment variables](doc/configuration.md#support-environment-variables).

## Main structure
Alligator has many contexts for describing the collection data:
- **aggregate**: Collects metrics from software using various parsers from software
- **query**: Generates new metrics internally through PromQL queries or database queries
- **namespace**: Configures metric namespaces and optional emission limits (`max_emit`)
- **entrypoint**: Receives metrics via Pushgateway, Statsd or Graphite protocols. It also configures the listen policy of ports to pass metrics to Prometheus, or make queries to the internal Alligator API
- **lang**: Runs functions and methods from subprograms
- **x509**: Obtains metrics from PEM, JKS or PKCS certificate formats
- **action**: Runs commands in response to the metrics behaviour. It allows for proactive monitoring policy.
- **scheduler**: Configures the repeat time to run lang or actions by alligator.
- **resolver**: Configures Alligator DNS and enables collecting metrics from DNS resolution checks
- **persistence**: Saves metrics to the filesystem that enable preservation metrics between restarts
- **modules**: Loads dynamic C libraries (files with .so extension)
- **cluster**: Configures the cluster using the node group
- **puppeteer**: Configures the HTTP site stats collector using the puppeteer
- **chromecdp**: Collects browser loading statistics via Chrome DevTools Protocol (no Node.js)
- **threaded_loop**: Configures thread pool with activated event loops for particular tasks
- **grok**: Parse logs in metrics like Elasticsearch’s Grok parser.
- **mtail**: Parse logs in metrics using mtail-compatible scripts powered by amtail.
- **vrl**: Remap and enrich log events with Vector Remap Language (avrl) programs.
- **enrichment_table**: CSV and MaxMind lookup tables for VRL (`get_enrichment_table_record`); see [vrl](doc/vrl/README.md)
- **probe**: Prometheus-style blackbox probe modules served at `GET /probe`; see [probe](doc/probe.md)

Detailed information about the configuration file structure is in [configuration](doc/configuration.md).



## Entrypoint context
Please refer to the entrypoint context [explanation](doc/entrypoint.md).

Here's an example of a simple handler that can respond to Prometheus:
```
entrypoint {
    handler prometheus;
    tcp 1111;
}
```

## System metrics collecting
Please refer to the explanation of system context [here](doc/system.md).

Below is an example of a simple system context that collects CPU, baseboard, system-wide resources, memory, and network statistics:
```
system {
    base;
    disk;
    network;
}
```

## Aggregate context
Aggregator makes it possible to collect metrics from other sources or software via URL.

The aggregate context section runs periodic checks on resources and gets data, pushing it into the parser to generates metrics.

Directive format:
```
aggregate {
    <parser> <url> [<options>];
}
```


Here's a simple example of the aggregate context with blackbox checking of TCP/UDP and other resources, collecting metrics from a file and even a directory containing files, and also collect metrics from the Redis server:
```
aggregate_period 10s;
aggregate {
    # Blackbox checks
    blackbox tcp://google.com:80 add_label=url:google.com;
    blackbox tls://www.amazon.com:443 add_label=url:www.amazon.com;
    blackbox udp://8.8.8.8:53;
    blackbox http://yandex.ru;
    blackbox https://nova.rambler.ru/search 'env=User-agent:googlebot';
    prometheus_metrics file:///tmp/metrics-nostate.txt;
    blackbox file:///etc/ checksum=murmur3 file_stat=true calc_lines=true;
    redis tcp://localhost:6379/;
}
```

More information about the aggregate directive can be found in [aggregate](doc/aggregate.md).


# List of software parsers
- [rsyslog](https://github.com/alligatormon/alligator/blob/master/doc/parsers/rsyslog.md)
- [PostgreSQL](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md)
- [MongoDB](https://github.com/alligatormon/alligator/blob/master/doc/parsers/mongodb.md)
- [redis](https://github.com/alligatormon/alligator/blob/master/doc/parsers/redis.md)
- [clickhouse](https://github.com/alligatormon/alligator/blob/master/doc/parsers/clickhouse.md)
- [zookeeper](https://github.com/alligatormon/alligator/blob/master/doc/parsers/zookeeper.md)
- [memcached](https://github.com/alligatormon/alligator/blob/master/doc/parsers/memcached.md)
- [beanstalkd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/beanstalkd.md)
- [gearmand](https://github.com/alligatormon/alligator/blob/master/doc/parsers/gearmand.md)
- [haproxy](https://github.com/alligatormon/alligator/blob/master/doc/parsers/haproxy.md)
- [blackbox](https://github.com/alligatormon/alligator/blob/master/doc/parsers/blackbox.md)
- [uwsgi](https://github.com/alligatormon/alligator/blob/master/doc/parsers/uwsgi.md)
- [nats](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nats.md)
- [riak](https://github.com/alligatormon/alligator/blob/master/doc/parsers/riak.md)
- [rabbitmq](https://github.com/alligatormon/alligator/blob/master/doc/parsers/rabbitmq.md)
- [eventstore](https://github.com/alligatormon/alligator/blob/master/doc/parsers/eventstore.md)
- Celery [flower](https://github.com/alligatormon/alligator/blob/master/doc/parsers/flower.md)
- [powerdns](https://github.com/alligatormon/alligator/blob/master/doc/parsers/powerdns.md)
- [apache httpd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/apache-httpd.md)
- [druid](https://github.com/alligatormon/alligator/blob/master/doc/parsers/druid.md)
- [couchbase](https://github.com/alligatormon/alligator/blob/master/doc/parsers/couchbase.md)
- [couchdb](https://github.com/alligatormon/alligator/blob/master/doc/parsers/couchdb.md)
- [mogilefs](https://github.com/alligatormon/alligator/blob/master/doc/parsers/mogilefs.md)
- [moosefs](https://github.com/alligatormon/alligator/blob/master/doc/parsers/moosefs.md)
- [kubernetes](https://github.com/alligatormon/alligator/blob/master/doc/parsers/kubernetes.md)
- [prometheus\_metrics](https://github.com/alligatormon/alligator/blob/master/doc/parsers/prometheus_metrics.md)
- [json\_query](https://github.com/alligatormon/alligator/blob/master/doc/parsers/json_query.md)
- [squid](https://github.com/alligatormon/alligator/blob/master/doc/parsers/squid.md)
- [bind](https://github.com/alligatormon/alligator/blob/master/doc/parsers/named.md) (nameD)
- [gdnsd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/gdnsd.md)
- [tftp](https://github.com/alligatormon/alligator/blob/master/doc/parsers/tftp.md)
- [unbound](https://github.com/alligatormon/alligator/blob/master/doc/parsers/unbound.md)
- [syslog-ng](https://github.com/alligatormon/alligator/blob/master/doc/parsers/syslog-ng.md)
- [elasticsearch](https://github.com/alligatormon/alligator/blob/master/doc/parsers/elasticsearch.md)
- [opentsdb](https://github.com/alligatormon/alligator/blob/master/doc/parsers/opentsdb.md)
- [hadoop](https://github.com/alligatormon/alligator/blob/master/doc/parsers/hadoop.md)
- [snmp](https://github.com/alligatormon/alligator/blob/master/doc/parsers/snmp.md)
- [aerospike](https://github.com/alligatormon/alligator/blob/master/doc/parsers/aerospike.md)
- [lighttpd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/lighttpd.md)
- [ipmi](https://github.com/alligatormon/alligator/blob/master/doc/parsers/ipmi.md)
- [keepalived](https://github.com/alligatormon/alligator/blob/master/doc/parsers/keepalived.md)
- [mysql](https://github.com/alligatormon/alligator/blob/master/doc/parsers/mysql.md)
- [monit](https://github.com/alligatormon/alligator/blob/master/doc/parsers/monit.md)
- [nginx](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nginx.md)
- [nsd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nsd.md)
- [ntp](https://github.com/alligatormon/alligator/blob/master/doc/parsers/ntp.md)
- [nvidia-smi](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nvidia-smi.md)
- [auditd](doc/parsers/auditd.md)
- [cassandra](doc/parsers/cassandra.md)
- [sentinel](doc/parsers/sentinel.md)
- [patroni](doc/parsers/patroni.md)
- [pgbouncer](doc/parsers/postgresql.md#pgbouncer)
- [odyssey](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md#odyssey)
- [pgpool](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md#pgpool)
- [varnish](https://github.com/alligatormon/alligator/blob/master/doc/parsers/varnish.md)
- [wazuh](https://github.com/alligatormon/alligator/blob/master/doc/parsers/wazuh.md)


## Persistence
It's a directive that specifies the directory for saving metrics between restarts.
```
persistence {
    directory /var/lib/alligator;
}
```

## Modules
The `modules` context allows loading `.so` files into memory.
```
modules {
	postgresql /usr/lib64/libpq.so;
	mysql /usr/lib/libmysqlclient.so;
}
```

This feature is typically used in parsers or for `lang` contexts.

## Resolver
The resolver in Alligator provides flexible DNS configuration. It allows using DNS servers other than the OS default and adds DNS resolution metrics. See [DNS resolver](doc/resolver.md).


## Certificates monitoring
Please refer to the explanation of x509 [context](doc/x509.md).

Alligator checks certificate expiry on the filesystem (`x509` context) and on TLS connections (`aggregate`, `entrypoint`). Optional **CRL** and **OCSP** revocation checks apply to aggregate TLS probes, mTLS entrypoints, and filesystem collectors. Metrics include `x509_cert_expire_days`, `x509_cert_revocation_status`, and `ocsp_requests_total`.

Walkthrough config: [misc/examples/ocsp/alligator.conf](misc/examples/ocsp/alligator.conf).


## Queries
[Here](https://github.com/alligatormon/alligator/blob/master/doc/query.md) is an explanation of query context.

## Namespace
[Here](https://github.com/alligatormon/alligator/blob/master/doc/namespace.md) is an explanation of namespace context and `max_emit`.

## Lang
[Lang](https://github.com/alligatormon/alligator/blob/master/doc/lang.md) loads a shared library (`.so`) to collect metrics (C/C++/Go/Rust).

## Actions
Actions run commands in response to scheduler triggers or metric behaviour, and can export data to external databases. See [action](doc/action.md).

## Scheduler
The [scheduler](https://github.com/alligatormon/alligator/blob/master/doc/scheduler.md) is a tool that specifies settings to repeatedly run lang and action resources.

## Cluster
Cluster enables multi-node metric synchronization. See [cluster](doc/cluster.md).

## Puppeteer
Puppeteer collects HTTP site load statistics. See [puppeteer](doc/puppeteer.md).

## Chromecdp
Chromecdp collects browser loading statistics from Chrome or Chromium headless **without Node.js or the Puppeteer npm package**. Alligator starts Chrome once, connects over the Chrome DevTools Protocol (CDP) via a local WebSocket, and crawls each configured URL in an isolated incognito context on every collection cycle. Metrics are written directly into the alligator metric store with Prometheus-style names (`chromecdp_*`).

Requirements: a Chrome/Chromium binary with CDP support (for example `chromium-browser` or `chromium-headless` on EL7/EL8).

```
chromecdp {
    executable /usr/bin/chromium-browser;
    port 9222;
    log_level off;
    concurrency 25;
    batch_size 2;
    batch_interval 1s;

    https://example.com {
        timeout        10s;
        ttl            120s;
        console_events true;
        add_label {
            team    sre;
            service web-check;
        }
        metricstransform {
            include ^chromecdp_.*$ match_type regexp label source regex '^https?://([^/]+).*$' replacement '$1';
        }
    }
}
```

Emitted metrics include page availability, per-resource HTTP status, load duration and size, Chrome performance counters, Resource Timing API timings, and optional console or JavaScript error counters. Module options include `concurrency`, `batch_size`, `batch_interval`, `setup_budget`, and `post_nav_budget` for parallel batched crawls. Per-URL options (`timeout`, `ttl`, `add_label`, `metricstransform`, `log_level`, `screenshot`) match the `puppeteer` context where applicable. Crawl timing follows the global `aggregate_period`; a new full cycle starts only after the previous one completes.

With `log_level off` (default), alligator suppresses Chrome’s own stderr noise (D-Bus, GPU, and similar messages). Set `log_level info` or higher to see Chrome startup diagnostics when debugging.

Full documentation: [chromecdp.md](https://github.com/alligatormon/alligator/blob/master/doc/chromecdp.md). Comparison with `puppeteer` is described there as well.

## Threaded loop
Threaded loop enables the thread pools with activated event loops for particular tasks. Here is an [explanation](https://github.com/alligatormon/alligator/blob/master/doc/threaded-loop.md).

## Grok
Enables parsing log entries into metrics using Elasticsearch-style Grok patterns. See the [detailed explanation](https://github.com/alligatormon/alligator/blob/master/doc/grok.md)

## Mtail
Enables parsing log entries into metrics with mtail-compatible programs in the C runtime. See the [detailed explanation](https://github.com/alligatormon/alligator/blob/master/doc/mtail/README.md)

## VRL
Enables remapping and enriching log events with Vector Remap Language (avrl) programs. See the [detailed explanation](https://github.com/alligatormon/alligator/blob/master/doc/vrl/README.md)
