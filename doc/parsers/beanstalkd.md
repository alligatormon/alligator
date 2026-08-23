**Language / Язык:** [English](beanstalkd.md) | [Русский](../ru/parsers/beanstalkd.md)

## Beanstalkd

Collects Beanstalkd tube and server stats over TCP (default port **11300**).

### Connection URL

```
tcp://host[:11300]
```

### Example

```
aggregate {
    beanstalkd tcp://localhost:11300;
}
```

### Metrics

Server and tube counters become `beanstalkd_*` metrics.

Optional process / socket checks:

```
system {
    process beanstalkd;
    services beanstalkd.service;
}

query {
    expr 'count by (src_port, process) (socket_stat{process="beanstalkd", src_port="11300"})';
    make socket_match;
    datasource internal;
}
```
