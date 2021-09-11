# alligator
alligator is aggregator for system and software metrics

# Properties
- custom metrics collection
"default scrape only "up 1"
- support prometheus
- support scrape processes metrics
- support GNU/Linux (TODO support FreeBSD and Microsoft Windows)
- pushgateway protocol (with TTL expire when set in config, or HTTP header X-Expire-Time in seconds)
- statsd protocol
- multiservice scrape

# Services support
- redis
- sentinel
- memcached
- beanstalkd

# config description:
```
#/etc/alligator.conf
log_level 0..10;
ttl 0; # global ttl for metrics in sec, default 300

#prometheus entrypoint for metrics (additional, set ttl for this context metrics from statsd/pushgateway)
entrypoint prometheus {
	ttl 3600;
	tcp 1111;
	unixgram /var/lib/alligator.unixgram;
	unix /var/lib/alligator.unix;
	handler prometheus;
}

#configuration with reject metric label http_response_code="404":
entrypoint {
	reject http_response_code 404;
	ttl 86400;
	tcp 1111;

	allow 127.0.0.0/8; # support ACL mechanism
}

#system metrics aggregation
system {
	base; #cpu, memory, load avg, openfiles
	disk; #disk usage and I/O
	network; #network interface start
	process [nginx] [bash] [/[bash]*/]; #scrape VSZ, RSS, CPU, Disk I/O usage from processes
	smart; #smart disk stats
	firewall; # iptables scrape
	cpuavg period=5; # analog loadavg by cpu usage stat only with period in minutes
	packages [nginx] [alligator]; # scrape packages info with timestamp installed
	cadvisor [docker=http://unix:/var/run/docker.sock:/containers/json]; # for scrape cadvisor metrics
	services [nginx.service]; # for systemd unit status

	pidfile [tests/mock/linux/svc.pid]; # monitoring process by pidfile
	userprocess [root]; # monitoring process by username
	groupprocess [root]; # monitoring process by groupname
	cgroup [/cpu/]; # monitoring process by cgroup

	sysfs  /path/to/dir; # override path
	procfs /path/to/dir; # override path
	rundir /path/to/dir; # override path
	usrdir /path/to/dir; # override path
	etcdir /path/to/dir; # override path
}

#aggregator context (scrape from services)
#format:
#<parser> <url>;
#asynchronous, the crawler downloads the data from <url>, parser converts them into metrics
aggregate_period 10000 # in milliseconds
query_period 20000 # in milliseconds
aggregate backends {
	#REDIS and SENTINEL
	redis tcp://localhost:6379/;
	sentinel tcp://localhost:2/;
	sentinel tcp://:password@localhost:2/;
	#REDIS with password
	redis tcp://:pass@127.0.0.1:6379;
	redis unix://:pass@/tmp/redis.sock;
	#CKICKHOUSE (http proto support)
	clickhouse http://localhost:8123;
	#ZOOKEEPER
	zookeeper http://localhost
	#MEMCACHED
	memcached tcp://localhost:11211;
	memcached tls://127.0.0.1:11211 tls_certificate=/etc/memcached/server-cert.pem tls_key=/etc/memcached/server-key.pem;
	#BEANSTALKD
	beanstalkd tcp://localhost:11300;
	#GEARMAND
	gearmand tcp://localhost:4730;
	#HAPROXY TCP or unix socket stats
	haproxy tcp://localhost:9999;
	haproxy unix:///var/run/haproxy;
	#HTTP checks:
	http  http://example.com;
	#ICMP checks:
	icmp icmp://example.com;
	#BASH exec shell:
	process exec:///bin/curl http://example.com:1111/metrics;
	#PHP-FPM TCP socket
	php-fpm fastcgi://127.0.0.1:9000/stats?json&full;
	#PHP-FPM UNIX socket
	php-fpm unixfcgi:///var/run/php5-fpm.sock:/stats?json&full;
	#uWSGI TCP socket
	uwsgi tcp://localhost:1717;
	#uWSGI UNIX socket
	uwsgi unix:///tmp/uwsgi.sock;
	#NATS
	nats http://localhost:8222;
	#RIAK
	riak http://localhost:8098;
	#RABBITMQ
	rabbitmq http://guest:guest@localhost:15672;
	#EVENTSTORE
	eventstore http://localhost:2113;
	#FLOWER
	flower http://localhost:5555;
	#POWERDNS
	powerdns http://localhost:8081/api/v1/servers/localhost/statistics header=X-API-Key:test;
	powerdns http://localhost:8081/servers/localhost/statistics header=X-API-Key:test;
	#OPENTSDB
	opentsdb http://localhost:4242/api/stats;
	#ELASTICSEARCH
	elasticsearch http://localhost:9200;
	#AEROSPIKE
	aerospike tcp://localhost:3000;
	#MONIT
	monit http://admin:admin@localhost:2812;
	#FLOWER celery
	flower http://localhost:5555;
	#GDNSD
	gdnsd unix:///usr/local/var/run/gdnsd/control.sock
	#SYSLOG-NG
	syslog-ng unix:///var/lib/syslog-ng/syslog-ng.ctl
	# hadoop
	hadoop http://localhost:50075/jmx;
	#UNBOUND over unix socket
	unbound tls://unix:/var/run/unbound.sock tls_certificate=/etc/unbound/unbound_control.pem tls_key=/etc/unbound/unbound_control.key tls_ca=/etc/unbound/unbound_server.pem;
	#UNBOUND over tls socket
	unbound tls://localhost:8953 tls_certificate=/etc/unbound/unbound_control.pem tls_key=/etc/unbound/unbound_control.key tls_ca=/etc/unbound/unbound_server.pem;
	#JMX
	jmx service:jmx:rmi:///jndi/rmi://127.0.0.1:12345/jmxrmi;
	# IPMI metrics
	ipmi exec:///usr/bin/ipmitool;
	# TFTP
	tftp udp://localhost:69/ping;
	# NAMED (BIND)
	named http://localhost:8080;
	# SQUID
	squid http://localhost:3128;
	# LIGHTTPD status (status.status-url  = "/server-status")
        lighttpd_status http://localhost:8080/server-status;
	# LIGHTTPD statistics (status.statistics-url = "/server-statistics")
        lighttpd_statistics http://localhost:8080/server-statistics;
	# Apache HTTPD
        httpd http://localhost/server-status;
	# NSD
        nsd unix:///run/nsd/nsd.ctl;
	# scrape prometheus format openmetrics from other exporters
	prometheus_metrics http://localhost:9100;
	# parse json
	jsonparse http://localhost:9200/_stats;

	# KUBERNETES ENDPOINT SCRAPE
	kubernetes_endpoint https://k8s.example.com 'env=Authorization:Bearer TOKEN';
	# KUBERNETES INGRESS SCRAPE FOR HTTP BLACKBOX CHECKS
	kubernetes_ingress https://k8s.example.com 'env=Authorization:Bearer TOKEN';

	# Consul configuration/discovery
	consul-configuration http://localhost:8500;
	consul-discovery http://localhost:8500;

	# Zookeeper configuration
	zookeeper-configuration zookeeper://localhost:2181;

	# Etcd configuration
	etcd-configuration http://localhost:2379;

	# Blackbox checks
	blackbox tcp://google.com:80 add_label=url:google.com;
	blackbox tls://www.amazon.com:443 add_label=url:www.amazon.com;
	blackbox udp://8.8.8.8:53;
	blackbox http://yandex.ru;
	blackbox https://nova.rambler.ru/search 'env=User-agent:googlebot';

	# metrics scrape from file:
	prometheus_metrics file:///tmp/metrics-nostate.txt [notify=true];

	# file stat calc:
	blackbox file:///etc/ checksum=murmur3 file_stat=true calc_lines=true

    # kubeconfig scarpe certificate data
    kubeconfig file:///app/src/tests/system/kubectl/kubeconfig state=begin;

    # Druid
    druid http://localhost:8888 name=druid;
    druid_worker http://localhost:8091;
    druid_historical http://localhost:8083;
    druid_broker http://localhost:8082;

    # Couchbase
    couchbase http://user:pass@localhost:8091;

    # Couchdb
    couchdb http://user:pass@localhost:5984;

    # MogileFS
    mogilefs tcp://localhost:7001;

    # MooseFS
    moosefs exec:///app/src/tests/mock/moosefs/mfscli;
}
```

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

## Monitoring rsyslog impstats:
```
entrypoint {
	handler rsyslog-impstats;
	udp 127.0.0.1:1111;
}
```

rsyslog.conf:
```
module(
	load="impstats"
	interval="10"
	resetCounters="off"
	log.file="off
	log.syslog="on"
	ruleset="rs_impstats"
)

template(name="impformat" type="list") {
        property(outname="message" name="msg")
}

ruleset(name="rs_impstats" queue.type="LinkedList" queue.filename="qimp" queue.size="5000" queue.saveonshutdown="off") {
        *.* action (
                type="omfwd"
                target="127.0.0.1"
                port="1111"
                protocol="udp"
                Template="impformat"
        )
}
```

Monitoring NameD stats:
named.conf:
```
statistics-channels {
    inet 127.0.0.1 port 8080 allow {any;};
};
```

in zone context add statistics counting:
```
zone "localhost" IN {
        type master;
        file "named.localhost";
        allow-update { none; };
        zone-statistics yes;
};
```

## Monitoring nginx upstream check module
https://github.com/yaoweibin/nginx_upstream_check_module
```
aggregate backends {
	nginx_upstream_check http://localhost/uc_status;
}
```

nginx.conf:
```
	location /uc_status {
		check_status;
	}
```
alligator.conf:
```
aggregate {
	named http://localhost:8080;
}
```

PostgreSQL support by user queries:
```
modules {
	postgresql /usr/lib64/libpq.so;
}

aggregate {
	postgresql postgresql://postgres@localhost name=pg;
}

query {
	expr "SELECT pg_database.datname dbname, pg_database_size(pg_database.datname) as size FROM pg_database";
	field size;
	datasource pg;
	make postgresql_databases_size;
}
query {
	expr "SELECT count(datname) FROM pg_database";
	field count;
	datasource pg;
	make postgresql_databases_count;
}
query {
	expr "SELECT now() - pg_last_xact_replay_timestamp() AS replication_delay";
	field replication_delay;
	datasource pg;
	make postgresql_replication_lag;
}
```

MySQL support by user queries:
```
modules {
	mysql /usr/lib/libmysqlclient.so;
}

aggregate {
	sphinxsearch mysql://127.0.0.1:9313;
	mysql mysql://user:password@127.0.0.1:3306 name=mysql;
}

query {
	expr 'SELECT table_schema "db_name", table_name "table", ROUND(SUM(data_length), 1) "mysql_table_size", ROUND(SUM(index_length), 1) "mysql_index_size", table_rows "mysql_table_rows" FROM information_schema.tables  GROUP BY table_schema';
	field mysql_table_size mysql_index_size mysql_table_rows;
	make mysql_db_size;
	datasource mysql;
}
query {
	expr 'SELECT command command, state state, user user, count(*) mysql_processlist_count, sum(time) mysql_processlist_sum_time FROM information_schema.processlist WHERE ID != connection_id() GROUP BY command,state, user';
	field mysql_processlist_count mysql_processlist_sum_time;
	make mysql_processlist_stats;
	datasource mysql;
}
```

MongoDB support:
```
modules {
	mongodb /usr/local/lib64/libmongoc-1.0.so;
}

aggregate {
	mongodb 'mongodb://localhost:27017/?appname=executing-example';
}
```

# StatsD mapping:
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
                match glob;
        }
}
```

# Call external methods in different languages (now support only java)
```
modules {
	jvm /usr/lib64/libjvm.so;
	postgresql /lib64/libpq.so.5;
	mysql /usr/lib64/mysql/libmysqlclient.so;
	mongodb /usr/local/lib64/libmongoc-1.0.so;
	rpm /usr/lib64/librpm.so.3;
}
lang {
	lang java;
	classpath dfvfvr;
	classname alligatorJmx;
	method getJmx;
	arg service:jmx:rmi:///jndi/rmi://127.0.0.1:12345/jmxrmi;
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
## Centos 7, Centos 8
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

Ubuntu 16.04:
```
echo 'deb https://packagecloud.io/amoshi/alligator/ubuntu/ xenial main' | tee /etc/apt/sources.list.d/alligator.list
```

Ubuntu 18.04:
```
echo 'deb https://packagecloud.io/amoshi/alligator/ubuntu/ bionic main' | tee /etc/apt/sources.list.d/alligator.list
```

Ubuntu 20.04:
```
echo 'deb https://packagecloud.io/amoshi/alligator/ubuntu/ focal main' | tee /etc/apt/sources.list.d/alligator.list
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

For more examples check URL with tests: https://github.com/alligatormon/alligator/tree/master/src/tests/system
