# Grok
This context allows you to parse logs into metrics like Elasticsearch’s Grok parser.


## key
Default: -\
Plural: no

Specifies the name of the context. It can be used as a reference in the scheduler or in the API.


## name
Default: -\
Plural: no

The name of the metric.


## match
Default: -\
Plural: no

A template used to match a log string. The captured fields are extracted and added as labels.


## bucket
Default: -\
Plural: yes

Enables the separation of input metrics into multiple buckets.


## le
Default: -\
Plural: yes

Enables the separation of input metrics into multiple LE buckets.


## quantile
Default: -\
Plural: yes

Enables the calculation of quantiles using the metric values.


## counter
Default: -\
Plural: yes

Adds additional metric counters for each log line.


This context should be used together with the global grok\_patterns option:
```
grok_patterns /etc/grok-patterns/patterns.conf;
```

This option accepts an array of file paths containing predefined data-type templates.
The file /etc/grok-patterns/patterns.conf provides a set of default patterns.
Users can override these defaults or add their own patterns by specifying additional files as the second and subsequent arguments.


## Overview
The examples of usage will be provided in the documentation, which will appear soon.

```
aggregate {
    grok file:///var/log/dmesg name=dmesg state=stream;
}
grok_patterns /etc/grok-patterns/patterns.conf;
grok {
  key dmesg;
  name dmesg_event;
  match '%{TIMESTAMP}] %{WORD:process}';
}
```

Alternatively, you can use this as a local UDP endpoint that parses logs into metrics:
```
entrypoint {
        udp 1112;
        handler grok;
        grok customword;
}

grok_patterns /etc/grok-patterns/patterns.conf;

grok {
  key customword;
  name customword;
  match '%{WORD:word}';
}
```

You can test it as follows:
```bash
$ echo something | nc -u 0.0.0.0 1112
^C
$ curl -s localhost:1111 | grep custom
customword {word="something"} 1.000000
$
```

## Example with nginx:
```
aggregate {
    grok file:///var/log/nginx-access.log name=nginx log_level=off;
}

grok {
  key nginx;
  name nginx_log;
  match '%{IPORHOST:client_ip} - %{DATA} \[%{HTTPDATE}\] "%{DATA:request}" %{NUMBER:status} %{NUMBER:bytes} "%{DATA}" "%{DATA}" utadr="%{DATA:upstream}" rt=%{DATA:response_time} ut="%{NUMBER:upstream_time}"';
  counter nginx_log_response_bytes bytes;
  quantiles nginx_log_response_time response_time 0.999 0.95 0.9;
  le nginx_log_upstream_time upstream_time 500 1000 10000;
}
```
