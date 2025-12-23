# Aggregator
The aggregator feature provides the opportunity to collect metrics from other software.\
It consists of two main parts: `aggregator` and `parser`.

<br>
<p align="center">
<h1 align="center" style="border-bottom: none">
    <img alt="alligator-cluster-entrypoint" src="/doc/images/aggregator.jpeg"></a><br>
</h1>
<br>

Alligator eventloop provides asynchronous methods for collecting metrics from various sources and passing them to custom parsers for each software:
<br>
<h1 align="center" style="border-bottom: none">
    <img alt="alligator-cluster-aggregate" src="/doc/images/parsers.jpeg"></a><br>
</h1>

<br>
<br>
</p>

The aggregator indludes async methods to get stats using various schemas/protocols:
- HTTP (http://) and HTTPS (https://). Enables the HTTP and HTTPS clients to get body.
- TCP (tcp://). Enables the TCP client to get body.
- TLS (tls://). Enables the TLS client to get body.
- UDP (udp://). Enables the UDP client to get body.
- unix (unix://) and unixgram (unixgram://). Enables the Unix-socket over SOCK\_STREAM and SOCK\_DGRAM clients to get the body.
- file (file://). Enables the file read to get body.
- exec (exec://). Enables the execution of an external program and read the stdout to the parser.

The parser gets the body after the aggregator works on it and parses it into metrics.

## Overview
```
aggregate {
    <parser1> <url1> [arg1] [arg2] ... [argN];
    <parser2> <url2> [arg1] [arg2] ... [argN];
    ...
    <parserM> <urlM> [arg1] [arg2] ... [argN];
}
```

## env
Default: -
Plural: yes

Specifies to request HTTP headers for HTTP/HTTPS protocols or environment variables for running external processes (exec).\
Here is an example of usage:
```
aggregate {
    blackbox https://google.com env=Connection:close env=User-agent:alligator;
}
```

## key
Default: automatically generates from the URL
Plural: no

Resets the key, a unique variable that usually generates automatically and is used as a primary key in the metrics connected to the aggregator's or API's working.


## name
Default: -\
Plural: no

Sets the name of the current aggregate object. This name allows the query to refer to the current aggregator.


## lang
Default: -\
Plural: no

The [lang](https://github.com/alligatormon/alligator/blob/master/doc/lang.md) option specifies the context that operates the received body with custom functions via external modules.

# follow\_redirects
Default: 0
Plural: no

The follow\_redirects option specifies the maximum number of following redirects.

## tls\_certificate
Default: -\
Plural: no

Specifies the path of the client x509 certificate for creating mTLS sessions.

## tls\_key
Default: -\
Plural: no

Specifies the path of the client x509 key for creating TLS sessions.


## tls\_ca
Default: -\
Plural: no

Specifies the certification authority (CA) x509 certificate path to verify this TLS session.


## tls\_server\_name
Default: -\
Plural: no

Specifies the TLS servername field when it is necessary.


## timeout
Default: 5s\
Plural: no

Specifies the timeout for the request.
More information about units that user can specify in configuratino can be obtained from configuration [doc](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-units-for-time-data-in-configuration-file).


## calc\_lines
Default: false\
Plural: no

Enables the calculation of lines in files within the directory.


## checksum
Default: false\
Plural: no
Available values:
- murmur3
- crc32

Enables the calculation of checksums for files within the directory.


## notify
Default: false\
Plural: no\
Available values:
- true
- false
- only

Enables the inotify mechanisms to check for updates of files within the directory. When set to `only`, the directive runs the file reader using notifications only and disables the scheduler.


## state
Default: stream\
Plural: no\
Possible values:
- begin
- save
- stream
- forget

Enables different read initiation modes for Alligator, both on restart and during runtime:
- **begin**: On startup, reads the file from the beginning, then reads only appended data.
- **save**: Saves the file offset between Alligator restarts and reads only appended data.
- **stream**: Starts reading from the current end of the file and reads only appended data.
- **forget**: Always reads the file from the beginning.


## pingloop
Default: 0\
Plural: no\
Possible values:
- {number}

Pingloop allows a blackbox handler to ping resource more than once.


## log\_level
Default: off\
Plural: no

Specify of the level of logging for the aggregator. Units for this option are explained in this [document](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-log-levels)


## threaded_loop_name
Default: -\
Plural: no

Specifies the declared name of the threaded loop. More information about the threaded loop can be found in the [threaded_loop](https://github.com/alligatormon/alligator/blob/master/doc/threaded-loop.md) documentation. This enables the thread pool for a particular aggregator and ensures that the main thread does not process it.


## stdin
Specifies the body to be passed to the stdin of the called script.


# cluster
Default: -\
Plural: no

Specify the cluster name for crawling metrics. More information about cluster can be found in the [cluster](https://github.com/alligatormon/alligator/blob/master/doc/cluster.md) documentation.

# instance
Default: -\
Plural: no

Specify the cluster current instance name for crawling metrics. More information about cluster can be found in [cluster](https://github.com/alligatormon/alligator/blob/master/doc/cluster.md) documentation.


## pquery
Default: -\
Plural: no

Specifies the JSON query for the json\_query parser. More information can be found in the [json\_query](https://github.com/alligatormon/alligator/blob/master/doc/parsers/json_query.md) documentation.

## period
Default: -\
Plural: no

Specifies the time interval for the repeated calling of the request.\
More information about units that user can specify in configuratino can be obtained from configuration [doc](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-units-for-time-data-in-configuration-file).


## resolve
Default: -\
Plural: no

Specify the domain name for resolution. More information about DNS requests in alligator can be found in [resolver](https://github.com/alligatormon/alligator/blob/master/doc/resolver.md) documentation.

### add\_label
Default: -\
Plural: yes
This option provides the opportunity to manipulate extra labels.

For example, this configuration adds two extra labels for each metric:
```
aggregate {
    elasticsearch http://localhost:9200 add_label=instance:localhost add_label=name:localcluster;
    elasticsearch http://external:9200 add_label=instance:external add_label=name:extcluster;
}
```


## Available parsers
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

## Example of usage with TLS options
```
aggregate {
    prometheus_metrics https://consulnode:8501/v1/agent/metrics?format=prometheus ca_certificate=/etc/consul.d/ca.crt tls_certificate=/etc/consul.d/server.crt tls_key=/etc/consul.d/server.key tls_server_name=consulcluster env=X-Consul-Token:XXXX;
}
```

## Example of usage checking directory for changes:
```
aggregate {
    blackbox file:///etc/ calc_lines=true file_stat=true checksum=murmur3;
}
```

These configuration generates metrics for each file in the directory:
```
file_lines {path="/etc/fuse.conf"} 17
file_stat_time {type="birthtime", path="/etc/fuse.conf"} 1676453256
file_stat_time {type="mtime", path="/etc/fuse.conf"} 1648043594
file_stat_time {type="ctime", path="/etc/fuse.conf"} 1676453256
file_stat_mode {type="regular", user="root", path="/etc/fuse.conf", group="root", int="100644", mode="rw-r--r--"} 33188
file_stat_modify_count {path="/etc/fuse.conf"} 3
file_stat_size {path="/etc/fuse.conf"} 694
file_checksum {path="/etc/fuse.conf", hash="murmur3"} 1032033040
```

Please note that when parsing files in a directory, the directory path must end with '/' as follows: `file:///var/log/`. This enables the directory walk mode. By default, single file checking is enabled.


## Example of usage reading metrics from a file:
```
aggregate {
    prometheus_metrics file:///var/run/keepalived_time_state state=save notify=true;
}
```

## Period by default
The configuration file allows to specify the default period of time to check resources in aggregate context:
```
aggregate_period 10s;
```

* The [service\_discovery](https://github.com/alligatormon/alligator/blob/master/doc/service-discovery.md) allows for retrieving configuration from Consul or etcd instances.
