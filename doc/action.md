# Action
This module provides the capability to run commands and export metrics to other database and scripts.\
Action can be triggered by metric behavior or scheduled for running.\
This context can be used multiple times within the configuration.

<br>
<p align="center">
<h1 align="center" style="border-bottom: none">
    <img alt="alligator-cluster-entrypoint" src="/doc/images/action.jpeg"></a><br>
</h1>
<br>
<br>
</p>


## Overview

```
action {
    name <name>;
    expr <epression or url>;
    ns <namespace>;
    work_dir <working directory>;
    serializer <serializer>;
    follow_redirects <redirects>;
    engine <engine>;
    index_template <index_template>;
}
```

## name
Specifies the name of the context. It can be used as a reference for others contexts (such as a query or scheduler), or in the API.


## expr
An expression that takes the command to run or URL.

An example of using it to run nginx if the port doesn't listen:
```
query {
    expr 'count by (src_port, process) (socket_stat{process="nginx", src_port="80"}) == 0';
    make nginx_exists;
    datasource internal;
    action no_nginx;

}
action {
    name no_nginx;
    expr 'exec://systemctl start nginx';
}
```

Another example that can repeatedly run script and give serialized metrics in json format to stdin of the script:
```
scheduler {
  name sched-script;
  period 15s;
  datasource internal;
  action run-script;
}

action {
  name run-script;
  serializer json;
  expr /usr/bin/script.sh;
}
```

## ns
When the database have internal namespaces (e.g., databases in a Relational DB), this field specifies the name of that namespace.


## dry\_run
The dry-run option helps you find the best way to work with your own commands and queries without executing them.


## work\_dir
Specifies the working directory to run external software using the command line.


## serializer
Specifies the serializer for the output data. The output body have to be synchronized with the input data source.
Available serializers include:
- json
- dsv
- graphite
- statsd
- dogstatsd
- carbon2
- influxdb
- clickhouse
- postgresql
- elasticsearch
- dynatrace
- otlp
- otlp\_protobuf

In case of a simple HTTP request, the body will be passed in the HTTP POST body.
This option also specifies the database connector that will be used for connection.

OTLP implementation supported only HTTP protocol with json and protobuf encoding.


For example, next script will send all metrics to the pushgateway and statsd repeatedly:
```
scheduler {
  name sched-graphite;
  period 5s;
  datasource internal;
  action to-graphite;
}

action {
  name to-graphite;
  serializer graphite;
  expr udp://localhost:1112;
}

scheduler {
  name sched-pushgateway;
  period 15s;
  datasource internal;
  action to-pushgateway;
}

action {
  name to-pushgateway;
  serializer openmetrics;
  expr tcp://localhost:9091/metrics;
}

scheduler {
  name sched-statsd;
  period 5s;
  datasource internal;
  action to-statsd;
}

action {
  name to-statsd;
  serializer statsd;
  expr udp://localhost:1112;
}

scheduler {
  name sched-dogstatsd;
  period 5s;
  datasource internal;
  action to-dogstatsd;
}

action {
  name to-dogstatsd;
  serializer dogstatsd;
  expr udp://localhost:1113;
}

scheduler {
  name sched-dynatrace;
  period 15s;
  datasource internal;
  action to-dynatrace;
}

action {
  name to-dynatrace;
  serializer dynatrace;
  expr http://localhost:14499/metrics/ingest;
}

scheduler {
  name sched-otlp;
  period 15s;
  datasource internal;
  action to-otlp;
}

action {
  name to-otlp;
  serializer otlp_protobuf;
  expr http://localhost:4318/v1/metrics;
}
```

## follow\_redirects
Specifies the maximum number of redirects to follow when making HTTP requests. The default value is zero, which means never follow the redirects.


## engine
Provides the capability to set the engine for creating tables in Clickhouse.

```
action {
    name to-clickhouse;
    expr http://localhost:8123;
    serializer clickhouse;
    engine ENGINE=MergeTree ORDER BY timestamp;
}
```

## index\_template
Provides the capability to set an index name for ElasticSearch using a template. This option supports strftime formatting, allowing for dynamically changeable opportunities at runtime.\
An example of usage is provided below:

```
action {
    name to-elastic;
    expr http://localhost:9200/_bulk;
    serializer elasticsearch;
    index_template alligator-%Y-%m-%d;
}
```


## metric_name_transform_pattern
/**
`metric_name_transform_pattern` and `metric_name_transform_replacement` define rules for transforming metric names before metrics are exported via an action.

- `metric_name_transform_pattern`: Specifies a regular expression (PCRE syntax) that matches metric names. It is used to identify all or part of metric names that should be rewritten or normalized. Only the **first** match in each name is used.

- `metric_name_transform_replacement`: Specifies a replacement string for renaming metric names. It can use `$1`, `$2`, ... to insert text from the corresponding capturing groups in the `metric_name_transform_pattern`.

**Example:**
```
action {
  name to-elastic;
  expr http://localhost:9200/_bulk;
  serializer elasticsearch;
  metric_name_transform_pattern ^stats\.(.*)$;
  metric_name_transform_replacement custom.$1;
}
```
In this example, any metric name starting with `stats.` will be rewritten to start with `custom.` (e.g., `stats.http.requests` → `custom.http.requests`).

**Notes:**
- If `metric_name_transform_pattern` and `metric_name_transform_replacement` are not set, metric names are kept as-is.
- Only one pair of pattern and replacement can be used per action.
- Setting only `metric_name_transform_pattern` without a corresponding replacement will result in no transformation.
- Patterns are case-sensitive.

See also: [src/metric/transform.c](https://github.com/alligatormon/alligator/blob/master/src/metric/transform.c) for implementation details.
*/


## env
Adds HTTP headers for `http`/`https` actions (they are merged into the outgoing request). For `exec://` actions, the same entries are supplied as environment variables for the subprocess.

In plain configuration, each line is `env 'Header-Name:value';` (or `header …`, which is an alias). The header name and value are separated by the **first** colon; repeat `env` for multiple headers. In JSON, use an object, for example `"env": { "Authorization": "Api-Token …" }`.

For example:

```
scheduler {
  name sched-dynatrace;
  period 15s;
  datasource internal;
  action to-dynatrace;
}

action {
  name to-dynatrace;
  serializer dynatrace;
  expr https://xxxx.live.dynatrace.com/api/v2/metrics/ingest;
  env 'Authorization:Api-Token XXXXXXXXX';
  env 'Content-Type: text/plain; charset=utf-8';
}
```

## log_level
Optional. When set, it becomes the `log_level` on the oneshot `context_arg` for this action (client and parser logging for that run). When omitted, the oneshot context uses the server-wide `log_level` from the main configuration.

Allowed values are the same as for the rest of Alligator; see [Available log levels](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-log-levels).

For example:

```
action {
  name to-dynatrace;
  serializer dynatrace;
  expr https://xxxx.live.dynatrace.com/api/v2/metrics/ingest;
  env 'Authorization:Api-Token XXXXXXXXX';
  env 'Content-Type: text/plain; charset=utf-8';
  log_level trace;
}
```
