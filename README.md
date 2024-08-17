# alligator
alligator is aggregator for system and software metrics

# Properties
- custom metrics collection
- support prometheus
- support scrape processes metrics
- support GNU/Linux and FreeBSD systems
- extended pushgateway protocol
- statsd protocol
- graphite protocol
- multiservice scrape


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
- **aciton**: Runs commands in response to the metrics behaviour. It allows for proactive monitoring policy.
- **scheduler**: Configures the repeat time to run lang or actions by alligator.
- **resolver**: Configures the DNS system of Alligator and allows to the collection of metrics from DNS servers for resolution
- **persistence**: Saves metrics to the filesystem that enable preservation metrics between restarts.
- **modules**: Loads dynamic C libraries (files with .so extension)

## Available log levels
- off
- fatal
- error
- warn
- info
- debug
- trace

## Log destinations
```
log_dest <dest>;
```

Destination can be standart streams of a Unix OS, a file or a UDP port. For example this directive can be set as follows:
```
- stdout
- stderr
- file:///var/log/messages
- udp://127.0.0.1:514
```

## Available units for time data in configuration file:
- ms
- s
- h
- d


## /etc/alligator.conf
Bellow is an example of the structure configuration file for Alligator:
```
log_level <level>;
ttl <time>;

entrypoint {
    <options>;
}

resolver {
    <server1>;
    <server2>;
    ...
    <serverN>;
}

system {
    <option1>;
    <option2>;
    ...
    <optionN> [<param1>] ... [<paramN>];
}

aggregate {
    <option1>;
    <option2>;
    ...
    <optionN>;
}

query {
    <query1 options>;
}

query {
    <query2 options>;
}

action {
    <action1 options>;
}

action {
    <action2 options>;
}

scheduler {
    <scheduler1 options>;
}

scheduler {
    <scheduler2 options>;
}

x509 {
    <certificate options>;
}

x509 {
    <certificate options>;
}
```

## Entrypoint context
Please refer to the entrypoint context explanation [here](https://github.com/alligatormon/alligator/blob/master/doc/entrypoint.md).

Here's an example of a simple handler that can respond to Prometheus:
```
entrypoint {
	handler prometheus;
    tcp 1111;
}
```

## System metrics collecting
Please refer to the explanation of system context in [here](https://github.com/alligatormon/alligator/blob/master/doc/system.md).

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
format:
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

## persistence directory for saving metrics between restarts
```
persistence {
	directory /var/lib/alligator;
}
```

## for monitoring PEM certs
```
x509
{
	name nginx-certs;
	path /etc/nginx/;
	match .crt;
}
```

## for monitoring JKS certs
```
x509 {
	name system-jks;
	path /app/src/tests/system/jks;
	match .jks;
	password password;
	type jks;
}
```

## Internal queries (example: check if port is listen)
```
query_period 20s;
query {
	expr 'count by (src_port, process) (socket_stat{process="dockerd", src_port="8085"})';
	make socket_match;
	datasource internal;
}
query {
	expr 'count by (src_port, process) (socket_stat{process="dockerd", src_port="8080"})';
	make socket_match;
	datasource internal;
}
```

## Monitoring rsyslog explained [here](https://github.com/alligatormon/alligator/blob/master/doc/rsyslog.md)
## Monitoring bind (NameD server) explained [here](https://github.com/alligatormon/alligator/blob/master/doc/named.md)
## Monitoring nginx upstream check module explained [here](https://github.com/alligatormon/alligator/blob/master/doc/nginx_upstream_check.md)
## Monitoring PostgreSQL by queries module explained [here](https://github.com/alligatormon/alligator/blob/master/doc/postgresql.md)
## Monitoring MySQL by queries module explained [here](https://github.com/alligatormon/alligator/blob/master/doc/mysql.md)
# Call external methods in different languages (now support duktape, lua, mruby and .so files (c/rust/c++)
```
lang {
    lang lua;
    method metrics_response;
    file /app/src/tests/system/lang/response.lua;
    arg "6";
    log_level 1;
    key lua_response;
}
```

# Support environment variables
__ is a separator of contexts
Example:
```
export ALLIGATOR__ENTRYPOINT0__TCP0=1111
export ALLIGATOR__ENTRYPOINT0__TCP1=1112
export ALLIGATOR__TTL=1200
export ALLIGATOR__LOG_LEVEL=0
export ALLIGATOR__AGGREGATE0__HANDLER=tcp
export ALLIGATOR__AGGREGATE0__URL="tcp://google.com:80"
```
Converts to config:
```
{
  "entrypoint": [
    {
      "tcp": [
        "1111",
        "1112"
      ]
    }
  ],
  "ttl": "1200",
  "log_level": "0",
  "aggregate": [
    {
      "handler": "tcp",
      "url": "tcp://google.com:80"
    }
  ]
}
```


# Distribution
## Docker
docker run -v /app/alligator.conf:/etc/alligator.conf alligatormon/alligator

## Centos 7, Centos 9
```
[rpm_alligator]
name=rpm_alligator
baseurl=https://packagecloud.io/amoshi/alligator/el/$releasever/$basearch
repo_gpgcheck=1
gpgcheck=0
enabled=1
gpgkey=https://packagecloud.io/amoshi/alligator/gpgkey
sslverify=1
sslcacert=/etc/pki/tls/certs/ca-bundle.crt
metadata_expire=300

```

## Ubuntu
```
apt install -y curl gnupg apt-transport-https && \
curl -L https://packagecloud.io/amoshi/alligator/gpgkey | apt-key add -
```

```
Ubuntu 20.04:
```
echo 'deb https://packagecloud.io/amoshi/alligator/ubuntu/ focal main' | tee /etc/apt/sources.list.d/alligator.list
```

Ubuntu 22.04:
```
echo 'deb https://packagecloud.io/amoshi/alligator/ubuntu/ jammy main' | tee /etc/apt/sources.list.d/alligator.list
```

Ubuntu 22.04:
```
echo 'deb https://packagecloud.io/amoshi/alligator/ubuntu/ noble main' | tee /etc/apt/sources.list.d/alligator.list
```

## Binary
https://dl.bintray.com/alligatormon/generic/

## FreeBSD
port: https://github.com/alligatormon/alligator-port
port for local build: clone project, cd to port-internal and make

or use pkgng repo file /usr/local/etc/pkg/repos/alligator.conf:
```
alligator: {
  url: "https://dl.bintray.com/alligatormon/freebsd10/" #freebsd 10
  url: "https://dl.bintray.com/alligatormon/freebsd11/" #freebsd 11
  mirror_type: "srv",
  enabled: yes
}
```

For more examples check URL with [tests](https://github.com/alligatormon/alligator/tree/master/src/tests/system)
