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
    add_label <key:value>;
    follow_redirects <redirects>;
    engine <engine>;
    index_template <index_template>;
}
```

## name
Specifies the name of the context. It can be used as a reference for others contexts (such as a query or scheduler), or in the API.


## expr
An expression that takes the command to run or URL.

When an action is run (for example from a scheduler or query), **`expr` must be non-empty**. If it is missing or blank, Alligator logs at fatal level and skips running that action (no crash).

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

## add_label
Adds static labels to every exported metric in the action.

In plain configuration, each line is `add_label key:value;`. Repeat `add_label` for multiple labels.

In JSON configuration, labels are represented as an object:

```
action {
  name to-dynatrace;
  serializer dynatrace;
  add_label labelname:labelvalue;
  add_label second_label:value;
  expr https://xxxx.live.dynatrace.com/api/v2/metrics/ingest;
  env 'Authorization:Api-Token XXXXXXXXX';
  env 'Content-Type: text/plain; charset=utf-8';
}
```

For example:

```
action {
  name to-otlp;
  serializer otlp_protobuf;
  expr http://localhost:4318/v1/metrics;
  add_label dcwq:sdc;
  add_label adcwwdc:dcedc;
}
```

## metricstransform
Rewrites **label keys and/or label values** during serialization for this action (export time). Stored metrics in Alligator keep their original keys and values; transforms are applied when the serializer builds the outgoing representation.

Use it to normalize outgoing labels before sending to external systems (for example, OTLP, OpenMetrics, JSON, ElasticSearch).

In plain config you can either pass a **JSON string** (as before) or a **native block** (no JSON) with the same semantics.

JSON string form:
```
action {
  name to-otlp;
  serializer otlp_protobuf;
  expr http://localhost:4318/v1/metrics;
  metricstransform '{"transforms":[{"include":"^.*$","match_type":"regexp","operations":[{"action":"update_label","label":"instance","value_actions":[{"regex":"^([^:]+):?.*$","replacement":"$1"}]}]}]}';
}
```

Native block form (one `;`-terminated statement per transform; implied `action` is `update_label`):
```
action {
  name to-otlp;
  serializer otlp_protobuf;
  expr http://localhost:4318/v1/metrics;
  metricstransform {
    include ^.*$ match_type regexp label instance regex '^([^:]+):?.*$' replacement '$1';
  };
}
```

Rename a label **key** on export (native keyword `new_label`; value rules are optional if you only change the key):
```
action {
  name to-otlp;
  serializer otlp_protobuf;
  expr http://localhost:4318/v1/metrics;
  metricstransform {
    include ^app_.*$ match_type regexp label k8s_pod_name new_label pod;
  };
}
```

Multiple changes in one `metricstransform` block:
```
action {
  name to-otlp;
  serializer otlp_protobuf;
  expr http://localhost:4318/v1/metrics;
  metricstransform {
    include ^node_.*$ match_type regexp label instance regex '^([^:]+):?.*$' replacement '$1';
    include http_requests_total match_type strict label path regex '^/api/v[0-9]+/(.*)$' replacement '/api/$1';
    include ^app_.*$ match_type regexp label source regex '^https?://([^/]+).*$' replacement '$1' replace_all false;
  };
}
```

Each `;`-terminated line inside the block becomes one transform rule.

Supported keywords inside the block (repeat `regex` / `replacement` or `new_value` for multiple `value_actions`; optional `replace_all true|false` after a pair applies to the last `value_action`):

- `include`, `metric`, `metric_regex`, `match_type` (`strict` or `regexp`)
- `label`, `label_regex`
- `new_label` — static rename of the matched label’s **key** on export (native plain only; see JSON for regex-based key edits)
- `regex`, `replacement` or `new_value`, `replace_all`

In **JSON** rules, each `operations[]` entry may also include:

- `new_label` — same as the native keyword (fixed new key name)
- `label_key_actions` — array of objects with the same shape as `value_actions` (`regex`, `replacement` or `new_value`, optional `replace_all`), applied in order to the **current label key** string

If both `label_key_actions` and `new_label` are present on the same operation, **`label_key_actions` takes precedence** for renaming the key (`new_label` is ignored).

Rule structure is OTel-style (`transforms`, `operations`, `value_actions`, optional key fields above) and supports regex capture groups in replacements (`$1`, `$2`, ...).

JSON example (regex on the key and on the value):
```
action {
  name to-json;
  serializer json;
  expr http://127.0.0.1:9/metrics;
  metricstransform '{"transforms":[{"include":"^.*$","match_type":"regexp","operations":[{"action":"update_label","label":"host","label_key_actions":[{"regex":"^host$","replacement":"instance"}],"value_actions":[{"regex":"^([^:]+):.*$","replacement":"$1"}]}]}]}';
}
```

### Matching metric names: include, metric, metric_regex

Each rule selects metrics using `include` (or legacy `metric`), or `metric_regex` (with `match_type: regexp` or the `metric_regex` field). During export, matching is done as follows:

1. **Serialized name first** — the metric name after this action’s [`metric_name_transform_pattern`](#metric_name_transform_pattern) / `metric_name_transform_replacement`, if those are set; otherwise the name is unchanged from storage.
2. **Stored name if different** — if the name kept inside Alligator is not the same as that serialized name, the same pattern is also tried against the stored (in-tree) metric key.

If either test succeeds, the rule applies (**OR** semantics). That way a single `include` can target either the short name on the wire (for example `memory_usage`) or the long key still stored internally (for example `ci12312312.memory_usage`) when both refer to the same series. A regexp can cover both forms in one pattern if needed.

### Serializers

The same `metricstransform` behavior (label **key and value** where the format exposes label names) runs for these serializers on actions: JSON, OTLP (JSON and protobuf), OpenMetrics, Graphite, Carbon2, InfluxDB line protocol, StatsD, DogStatsD, Dynatrace, Elasticsearch bulk, ClickHouse, Cassandra, and PostgreSQL.

The **DSV** serializer only applies `metricstransform` to **label values**; the delimited field order follows the stored label key order (key renames from `metricstransform` are not applied in DSV output).

On **[entrypoints](https://github.com/alligatormon/alligator/blob/master/doc/entrypoint.md#metricstransform)** and **[aggregates](https://github.com/alligatormon/alligator/blob/master/doc/aggregate.md#metricstransform)**, `metricstransform` runs at **ingest / aggregate** time; matching uses only the metric name as produced at that stage (no `metric_name_transform` dual-name logic). Key and value rules still update what gets **stored** in Alligator. The dual-name matching in the previous section applies only to **actions** at export time.

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
