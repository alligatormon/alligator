## Cassandra

Now Alligator doesn't support to collect statistics by queries. However, alligator supports the other methods to collect statistics from Cassandra - graphite and file exporters
```


Alligator also allows the pushing metric to Cassandra using `action` methods.\
It provides the capability to set an index name for Cassandra using a template. This option supports strftime formatting, allowing for dynamically changeable opportunities at runtime.\
An example of usage is provided below. In this example, metrics will be pushed to the ElasticSearch instance every 15 seconds:

```
aggregate {
        cassandra cassandra://user:password@127.0.0.1/app_data name=to-cassandra;
}

scheduler {
  name cassandra;
  period 15s;
  datasource internal;
  action to-cassandra;
}

action {
    name to-cassandra;
    expr cassandra;
    serializer cassandra;
}
```

Cassandra module in Alligator supports by user queries:
```
aggregate {
        cassandra cassandra://user:password@127.0.0.1/app_data name=from-cassandra;
}

query {
       name cassandra;
       expr 'SELECT value, name FROM metric';
       field value;
       datasource from-cassandra;
}
```
