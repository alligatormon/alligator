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


## quantiles
Default: -\
Plural: yes

Enables the calculation of quantiles using the metric values.


## counter
Default: -\
Plural: yes

Adds additional metric counters for each log line.

## splited_tags
Default: -\
Plural: no

Specifies the splited labels (actual for nginx array-variables like $upstream_addr, $upstream_response_time and i.e)

Format:
```
splited_tags <separator> [label1] [label2] ... [labelN];
```


## splited_inherit_tag
Default: -\
Plural: no

Specifies the inherited labels from common level tags.

Format:
```
splited_inherit_tag [label1] [label2] ... [labelN];
```


## splited_counter
Default: -\
Plural: yes

Adds additional metric counters for each log line. It specially works for array-variables with separator specified by third argument.

Format:
```
splited_counter <metric name> <label name> <separator>
```


## splited_quantiles
Default: -\
Plural: yes

Enables the calculation of quantiles using the metric values. It specially works for array-variables with separator specified by third argument.

Format:
```
splited_quantiles <metric name> <label name> <separator> [quantile1] [quantile2] ... [quantileN];
```


## splited_bucket
Default: -\
Plural: yes

Enables the separation of input metrics into multiple buckets. It specially works for array-variables with separator specified by third argument.

Format:
```
splited_bucket <metric name> <label name> <separator> [bucket1] [bucket2] ... [bucketN];
```



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
  match '%{IPORHOST:client_ip} - %{DATA} \[%{HTTPDATE}\] "%{DATA:request}" %{NUMBER:status} %{NUMBER:bytes} "%{DATA}" "%{DATA}" utadr="%{DATA:upstream_addr}" rt=%{DATA:response_time} ut="%{DATA:upstream_time}" us="%{WORD:upstream_status}';
  counter nginx_log_response_bytes bytes;
  quantiles nginx_log_response_time response_time 0.999 0.95 0.9;

  splited_tags ", " upstream_status upstream_addr;
  splited_inherit_tag server_name;
  splited_quantiles nginx_upstream_response_time upstream_time ", " 0.5 0.75 0.9;
}
```
