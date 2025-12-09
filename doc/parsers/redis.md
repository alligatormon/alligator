## Redis

Redis can work with a password or on a Unix socket. Alligator allows the collection of metrics from any of these configuration. The following example demonstrates different ways to connect with Redis:
```
aggregate {
    redis tcp://localhost:6379/;
    redis tcp://:pass@127.0.0.1:6379;
    redis unix:///tmp/redis.sock;
    redis unix://:pass@/tmp/redis.sock;
```

Monitoring of the Sentinel follows a is similar pattern:
```
aggregate {
    sentinel tcp://localhost:26379;
    sentinel tcp://:password@localhost:26379;
}
```

The next configuration only allows a ping request to check the availability of Redis. It can also be used by the `query` context.
```
aggregate {
    redis_ping tcp://localhost:6379 name=kv;
}

query {
    expr "MGET veigieMu ohThozoo ahPhouca";
    datasource kv;
    make redis_query;
}
```

## Dashboard
The system dashboard for Grafana + Prometheus is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-redis.json)
<img alt="Dashboard" src="/doc/images/dashboard-redis.jpg"><br>
