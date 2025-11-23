<h1 align="center" style="border-bottom: none">
    <img width="100" height="100" alt="Alligator" src="/doc/images/logo.min.png"></a><br>Alligator
</h1>

<p align="center">
Alligator is an aggregator for system and software metrics. It is an incredibly versatile tool, allowing anyone to effortlessly gather and aggregate metrics from a wide array of sources, including software, operating systems, and numerous other systems in infrastructure. Its capabilities empower users to comprehensively monitor and analyze the performance and behavior of servers. By seamlessly interfacing with diverse systems and platforms, Alligator enables users to gain visibility and insight into their infrastructure and applications.
<br>
<br>
</p>


# Installation
Alligator supports the GNU/Linux and FreeBSD systems.
How to install this, check the [distribution](https://github.com/alligatormon/alligator/blob/master/doc/distribution.md) doc.

For more examples check URL with [tests](https://github.com/alligatormon/alligator/tree/master/src/tests/system)

# Configuration description:
Alligator supports YAML, JSON or plain-text format. In examples we will consider only plain text format. For more information please refer to the detailed documentation or the tests.

When alligator starts, it tries to parse the /etc/alligator.json file. If this file is not found, it then tries to parse the /etc/alligator.yaml file. Otherwise, it falls back to parsing the /etc/alligator.conf file.

Additionally, Alligator attempts to parse the entire directory /etc/alligator/ for files with the .yaml, .json and .conf file extensions. This approach enables the use of includes. Users can combine all of these methods for representing configuration (YAML, JSON and plain text .conf).

The configuration file path can be passed as the first argument on the command line.
```
alligator <path_to_conf>
alligator <path_to_dir>
```

## Main structure
Alligator has many contexts for describing the collection data:
- **aggregate**: Collects metrics from software using various parsers from software
- **query**: Generates make new metrics internally through PromQL queries or from databases queries
- **entrypoint**: Receives metrics via Pushgateway, Statsd or Graphite protocols. It also configures the listen policy of ports to pass metrics to Prometheus, or make queries to the internal Alligator API
- **lang**: Runs functions and methods from subprograms
- **x509**: Obtains metrics from PEM, JKS or PCKS certificate formats
- **action**: Runs commands in response to the metrics behaviour. It allows for proactive monitoring policy.
- **scheduler**: Configures the repeat time to run lang or actions by alligator.
- **resolver**: Configures the DNS system of Alligator and allows to the collection of metrics from DNS servers for resolution
- **persistence**: Saves metrics to the filesystem that enable preservation metrics between restarts
- **modules**: Loads dynamic C libraries (files with .so extension)
- **cluster**: Configures the cluster using the node group
- **puppeteer**: Configures the HTTP site stats collector using the puppeteer
- **threaded_loop**: Configures thread pool with activated event loops for particular tasks
- **grok**: Parse logs in metrics like Elasticsearch’s Grok parser.

Detailed information about the configuration file structure stored in the [configuration](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md)



## Entrypoint context
Please refer to the entrypoint context [explanation](https://github.com/alligatormon/alligator/blob/master/doc/entrypoint.md).

Here's an example of a simple handler that can respond to Prometheus:
```
entrypoint {
    handler prometheus;
    tcp 1111;
}
```

## System metrics collecting
Please refer to the explanation of system context [here](https://github.com/alligatormon/alligator/blob/master/doc/system.md).

Bellow is an example of a simple configured system context in Alligator that enables the collection of CPU, baseboard, system-wide resouces, memory and network statistics:
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
    blackbox file:///etc/ checksum=murmur3 file_stat=true calc_lines=true
    redis tcp://localhost:6379/;
}
```

More information about the aggregate directive can be found at the following [document](https://github.com/alligatormon/alligator/blob/master/doc/aggregate.md).


# List of software parsers
- [rsyslog](https://github.com/alligatormon/alligator/blob/master/doc/parsers/rsyslog.md)
- [PostgreSQL](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md)
- [MySQL by queries module](https://github.com/alligatormon/alligator/blob/master/doc/parsers/mysql.md)
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
- [kubernetes](https://github.com/alligatormon/alligator/blob/master/doc/parsers/moosefs.md)
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
- [aerospike](https://github.com/alligatormon/alligator/blob/master/doc/parsers/aerospike.md)
- [lighttpd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/lighttpd.md)
- [ipmi](https://github.com/alligatormon/alligator/blob/master/doc/parsers/ipmi.md)
- [keepalived](https://github.com/alligatormon/alligator/blob/master/doc/parsers/keepalived.md)
- [mysql](https://github.com/alligatormon/alligator/blob/master/doc/parsers/mysql.md)
- [monit](https://github.com/alligatormon/alligator/blob/master/doc/parsers/monit.md)
- [nginx](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nginx.md)
- [nifi](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nifi.md)
- [nsd](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nsd.md)
- [ntp](https://github.com/alligatormon/alligator/blob/master/doc/parsers/ntp.md)
- [nvidia-smi](https://github.com/alligatormon/alligator/blob/master/doc/parsers/nvidia-smi.md)
- [patroni](https://github.com/alligatormon/alligator/blob/master/doc/parsers/patroni.md)
- [postgresql](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgesql.md)
- [pgbouncer](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md#pgbouncer)
- [odyssey](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md#odyssey)
- [pgpool](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md#pgpool)
- [varnish](https://github.com/alligatormon/alligator/blob/master/doc/parsers/varnish.md)


## Persistence
It's a directive that specifies the directory for saving metrics between restarts.
```
persistence {
    directory /var/lib/alligator;
}
```

## Modules
The `modules` context allows the loadingin of .so files into memory.
```
modules {
	postgresql /usr/lib64/libpq.so;
	mysql /usr/lib/libmysqlclient.so;
}
```

This feature is typically used in parsers or for `lang` contexts.

## Resolver
The resolver in Alligator provides capabilities for working with DNS flexible. It allows to use different DNS server that is configured in operating system and add functionality to resolve DNS names to the metrics. For more information, please refer to the [DNS resolver](https://github.com/alligatormon/alligator/blob/master/doc/resolver.md) in Alligator.


## Certificates monitoring
Please refer to the explanation of x509 [context](https://github.com/alligatormon/alligator/blob/master/doc/x509.md).


## Queries
[Here](https://github.com/alligatormon/alligator/blob/master/doc/query.md) is an explanation of query context.

## Lang
[Lang](https://github.com/alligatormon/alligator/blob/master/doc/lang.md) is a way to run other software to collect metrics.

## Actions
Actions provides the capability to run other software via the command in response to the scheduler or metric behaviour. It also provide capability to export data in others databases. Here is an [explanation](https://github.com/alligatormon/alligator/blob/master/doc/action.md).

## Scheduler
The [scheduler](https://github.com/alligatormon/alligator/blob/master/doc/scheduler.md) is a tool that specifies settings to repeatedly run lang and action resources.

## Cluster
Cluster enables the multi-node capabilities to synchronize metrics. [Here](https://github.com/alligatormon/alligator/blob/master/doc/cluster.md) is the more information about this.

## Puppeteer
Puppeteer enables the collector of site load statistics. In [this](https://github.com/alligatormon/alligator/blob/master/doc/puppeteer.md) document is the more information about this.

## Threaded loop
Threaded loop enables the thread pools with activated event loops for particular tasks. Here is an [explanation](https://github.com/alligatormon/alligator/blob/master/doc/threaded-loop.md).

## Grok
Enables parsing log entries into metrics using Elasticsearch-style Grok patterns. See the [detailed explanation](https://github.com/alligatormon/alligator/blob/master/doc/grok.md)
