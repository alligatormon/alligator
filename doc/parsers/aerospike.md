## Aerospike

To enable the collection of statistics from Aerospike, use the following option:
```
aggregator {
    aerospike tcp://localhost:3000;
}
```

Check the service port in configuration file. The service is [typically](https://aerospike.com/docs/server/operations/plan/network) listened on port 3000.
