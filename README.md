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
	ttl 1212;
	tcp 1111;
	unixgram /var/lib/alligator.unixgram;
	unix /var/lib/alligator.unix;
	handler prometheus;
}

#configuration with reject metric label http_response_code="404":
entrypoint prometheus {
	reject http_response_code 404;
	ttl 1212;
	tcp 1111;
	handler prometheus;
}

#system metrics aggregation
system {
	base; #cpu, memory, load avg, openfiles
	disk; #disk usage and I/O
	network; #network interface start
	process [nginx] [bash] [/[bash]*/]; #scrape VSZ, RSS, CPU, Disk I/O usage from processes
	smart; #smart disk stats
	firewall; # iptables scrape
	packages [nginx] [alligator]; # scrape packages info with timestamp installed
	cadvisor [docker=http://unix:/var/run/docker.sock:/containers/json]; # for scrape cadvisor metrics
	services; # for systemd unit status
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
	aerospike tcp://localhost:3000 namespace1 namespace2;
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
}```

MongoDB support:
```
modules {
	mongodb /usr/local/lib64/libmongoc-1.0.so;
}

aggregate {
	mongodb 'mongodb://localhost:27017/?appname=executing-example';
}```

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


# Distribution
## Docker
docker run -v /app/alligator.conf:/etc/alligator.conf alligatormon/alligator
## yum
```
[alligator-rpm]
name=alligator-rpm
baseurl=https://dl.bintray.com/alligatormon/rpm
gpgcheck=0
repo_gpgcheck=0
enabled=1
```

## deb
```
echo "deb https://dl.bintray.com/alligatormon/deb xenial main" | sudo tee -a /etc/apt/sources.list
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


TODO: scrape from all services
