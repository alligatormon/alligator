#aggregator context (scrape from services)
#format:
#<parser> <url>;
#asynchronous, the crawler downloads the data from <url>, parser converts them into metrics
aggregate_period 10000 # in milliseconds
query_period 20000 # in milliseconds
aggregate {
	#REDIS and SENTINEL
	redis tcp://localhost:6379/;
	sentinel tcp://localhost:2/;
	sentinel tcp://:password@localhost:2/;
	#REDIS with password
	redis tcp://:pass@127.0.0.1:6379;
	redis unix://:pass@/tmp/redis.sock;
	# REDIS only ping (or queries)
	redis_ping tcp://localhost:2/;
	#CKICKHOUSE (http proto support)
	clickhouse http://localhost:8123;
	#ZOOKEEPER
	zookeeper http://localhost;
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
