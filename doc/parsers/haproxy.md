## HAproxy

To enable the collection of statistics from HAproxy, use the following option:
### over tcp socket
```
aggregate {
    haproxy tcp://localhost:9999;
}
```

### over unix socket
```
aggregate {
    haproxy unix:///var/run/haproxy;
}
```
