## ElasticSearch

To enable the collection of statistics from ES, use the following option:
```
aggregate {
    elasticsearch http://localhost:9200;
}
```

If authentication is enabled, the user and password can be specified in the URL:
```
aggregate {
    elasticsearch http://user:password@localhost:9200;
}
```

Alligator also allows the pushing metric to ElasticSearch using `action` methods.\
It provides the capability to set an index name for ElasticSearch using a template. This option supports strftime formatting, allowing for dynamically changeable opportunities at runtime.\
An example of usage is provided below. In this example, metrics will be pushed to the ElasticSearch instance every 15 seconds:

```
scheduler {
  name sched-elastic;
  period 15s;
  datasource internal;
  action to-elastic;
}

action {
    name to-elastic;
    expr http://localhost:9200/_bulk;
    serializer elasticsearch;
    index_template alligator-%Y-%m-%d;
}
```

The `json-query` also supports the parsing JSON responses from various databases, including ElasticSearch.\
For example, it can be useful for requesting data from ElasticSearch and parse the responses into metrics:
```
aggregate {
    json_query 'http://localhost:9200/_search?q=something';
}
```
