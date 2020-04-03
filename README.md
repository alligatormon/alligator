# alligator
alligator is aggregator for system and software metrics

# Properties
- custom metrics collection
"default scrape only "up 1"
- support prometheus
- support scrape processes metrics
- support GNU/Linux (TODO support FreeBSD and Microsoft Windows)
- pushgateway protocol (TODO statsd and graphite support)
- multiservice scrape

# Services support
- redis
- sentinel
- memcached
- beanstalkd

# config description:
```
#/etc/alligator.conf
#prometheus entrypoint for metrics
entrypoint prometheus {
	tcp 1111;
	handler prometheus;
}

#system metrics aggregation
system {
	base; #cpu, memory, load avg, openfiles
	disk; #disk usage and I/O
	network; #network interface start
	process; #scrape VSZ, RSS, CPU, Disk I/O usage from processes
	smart; #smart disk stats
	vm; # scrape openvz, lxc and nspawn metrics
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
