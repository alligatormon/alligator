from statsd import StatsClient, TCPStatsClient
from time import sleep

statsd = TCPStatsClient(host='localhost',
                     port=1111,
                     prefix=None,
                     ipv6=False)

for i in range(100000):
	statsd.incr('baz')
	sleep(0.0001)
	statsd.incr('baz')
