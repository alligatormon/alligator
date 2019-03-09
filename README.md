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
}

#aggregator context (scrape from services)
#format:
#<parser> <url>;
#asynchronous, the crawler downloads the data from <url>, parser converts them into metrics
aggregate backends {
	#REDIS and SENTINEL
	redis tcp://localhost:6379/;
	redis tcp://localhost:2/;
	#CKICKHOUSE (http proto support)
	clickhouse http://localhost:8123;
	#ZOOKEEPER
	zookeeper http://localhost
	#MEMCACHED
	memcached tcp://localhost:11211;
	#BEANSTALKD
	beanstalkd tcp://localhost:11300;
	#GEARMAND
	gearmand tcp://localhost:4730;
	#HTTP checks:
	http  http://example.com;
	#ICMP checks:
	icmp icmp://example.com;
	#BASH exec shell:
	process exec:///bin/curl http://example.com:1111/metrics;
}
```

TODO: scrape from all services
