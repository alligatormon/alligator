Changelog

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
