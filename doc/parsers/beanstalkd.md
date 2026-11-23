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

Server and tube stats become `beanstalkd_*` metrics. Cumulative fields (`cmd_*`, `total_*`, `job_timeouts`, `uptime`, tube `cmd_*` / `total_jobs`) are COUNTERs with a `_total` suffix.

Optional process / socket checks:

```
system {
    process beanstalkd;
    services beanstalkd.service;
}

query {
    expr 'count by (src_port, process) (socket_listening{process="beanstalkd", src_port="11300"})';
    make socket_match;
    datasource internal;
}
```
