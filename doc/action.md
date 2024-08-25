# Action
This module provides the capability to run commands and export metrics to other database and scripts.\
Action can be triggered by metric behavior or scheduled for running.\
This context can be used multiple times within the configuration.


## Overview

```
action {
    name <name>;
    expr <epression or url>;
    ns <namespace>;
    work_dir <working directory>;
    serializer <serializaer>;
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


## work\_dir
Specifies the working directory to run external software using the command line.


## serializer
Specifies the serializer for the output data. The output body have to be synchronized with the input data source.
Available serializers include:
- json
- dsv
- graphite
- carbon2
- influxdb
- clickhouse
- postgresql
- elasticsearch

In case of a simple HTTP request, the body will be passed in the HTTP POST body.
This option also specifies the database connector that will be used for connection.


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
```

## follow\_redirects
Specifies the maximum number of redirects to follow when making HTTP requests. The default value is zero, which means never follow the redirects.


## engine
Provides the capability to set the engine for creating tables in Clickhouse.

```
action {
    name to-elastic;
    expr http://localhost:9200/_bulk;
    serializer elasticsearch;
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
