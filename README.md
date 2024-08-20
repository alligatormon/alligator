<h1 align="center" style="border-bottom: none">
    Alligator
    <img alt="Alligator" src="/doc/images/logo.jpeg"></a><br>
</h1>

# Alligator
Alligator is an aggregator for system and software metrics. It is an incredibly versatile tool, allowing anyone to effortlessly gather and aggregate metrics from a wide array of sources, including software, operating systems, and numerous other systems in infrastructure. Its capabilities empower users to comprehensively monitor and analyze the performance and behavior of servers. By seamlessly interfacing with diverse systems and platforms, Alligator enables users to gain visibility and insight into their infrastructure and applications.


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
- **aciton**: Runs commands in response to the metrics behaviour. It allows for proactive monitoring policy.
- **scheduler**: Configures the repeat time to run lang or actions by alligator.
- **resolver**: Configures the DNS system of Alligator and allows to the collection of metrics from DNS servers for resolution
- **persistence**: Saves metrics to the filesystem that enable preservation metrics between restarts
- **modules**: Loads dynamic C libraries (files with .so extension)
- **cluster**: Configures the cluster using the node group

Detailed information about the configuration file structure stored in the [configuration](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md)



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
- Monitoring rsyslog explained [here](https://github.com/alligatormon/alligator/blob/master/doc/rsyslog.md)
- Monitoring bind (NameD server) explained [here](https://github.com/alligatormon/alligator/blob/master/doc/named.md)
- Monitoring nginx upstream check module explained [here](https://github.com/alligatormon/alligator/blob/master/doc/nginx_upstream_check.md)
- Monitoring PostgreSQL by queries module explained [here](https://github.com/alligatormon/alligator/blob/master/doc/postgresql.md)
- Monitoring MySQL by queries module explained [here](https://github.com/alligatormon/alligator/blob/master/doc/mysql.md)


## Persistence
It's a directive that specifies the directory for saving metrics between restarts.
```
persistence {
	directory /var/lib/alligator;
}
```

## Certificate monitoring
Please refer to the explanation of x509 context [here](https://github.com/alligatormon/alligator/blob/master/doc/x509.md).


## Internal queries
[Here](https://github.com/alligatormon/alligator/blob/master/doc/query.md) is an explanation of query context.


## Lang
Lang is a way to run other software to collect metrics. [Here](https://github.com/alligatormon/alligator/blob/master/doc/lang.md) is an explanation.

## Cluster
Cluster enables the multi-node capabilities to synchronize metrics. [Here](https://github.com/alligatormon/alligator/blob/master/doc/cluster.md) is the more information about this.
