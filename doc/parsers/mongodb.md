## MongoDB

To enable the collection of statistics from MongoDB, use the following option:
```
aggregate {
    mongodb mongodb://localhost:27017 dbs=* colls=* collectors=*;
}
```

If authentication is enabled, the user and password can be specified in the URL:
```
aggregate {
    mongodb mongodb://user:password@localhost:27017 dbs=* colls=* collectors=*;
}
```

### Extra parameters
db=db1,db2
db=*
colls=coll1,coll2,coll3
colls=*
collectors=buildInfo,isMaster,replSetGetStatus,getDiagnosticData,coll,db,top
collectors=*

#### collectors
- buildInfo
- isMaster
- replSetGetStatus
- getDiagnosticData
- coll
- db

Alligator also allows the pushing metric to MongoDB using `action` methods.\
It provides the capability to set an index name for MongoDB using a template. This option supports strftime formatting, allowing for dynamically changeable opportunities at runtime.\
An example of usage is provided below. In this example, metrics will be pushed to the MongoDB instance every 15 seconds:

```
scheduler {
  name sched-mongo;
  period 15s;
  datasource internal;
  action to-mongo;
}

action {
    name to-mongo;
    expr http://localhost:9200/_bulk;
    serializer mongosearch;
    index_template alligator-%Y-%m-%d;
}
```

The `json-query` also supports the parsing JSON responses from various databases, including MongoDB.\
For example, it can be useful for requesting data from MongoDB and parse the responses into metrics:
```
aggregate {
    json_query 'http://localhost:9200/_search?q=something';
}
```
