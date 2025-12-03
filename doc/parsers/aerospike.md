## Aerospike

To enable the collection of statistics from Aerospike, use the following option:
```
aggregate {
    aerospike tcp://localhost:3000;
}
```

Check the service port in configuration file. The service is [typically](https://aerospike.com/docs/server/operations/plan/network) listened on port 3000.

## Dashboard
The aerospike dashboard for Grafana + Prometheus is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-aerospike.json)
<h1 align="center" style="border-bottom: none">
    <img alt="Alligator aerospike dashboard" src="/doc/images/dashboard-aerospike.jpg"></a><br>aerospike dashboard
</h1>
