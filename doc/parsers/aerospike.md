# Aerospike

Alligator collects Aerospike metrics through the Aerospike info protocol.

## Configuration

```
aggregate {
    aerospike tcp://localhost:3000;
}
```

Check the service port in configuration file. The service is [typically](https://aerospike.com/docs/server/operations/plan/network) listened on port 3000.

## Exported metrics

| Metric | Type | Description |
|--------|------|-------------|
| `aerospike_*` | gauge | Server statistics from the `statistics` info command. |
| `aerospike_status` | gauge | Node status from the `status` info command. |
| `aerospike_*` with `namespace` label | gauge | Per-namespace metrics from `namespace/{name}` info responses. |
| `aerospike_client`, `aerospike_batch`, `aerospike_scan`, and other grouped families | gauge | Namespace metrics grouped by operation type with an additional `type` label. |

All exported families include Prometheus `# HELP` and `# TYPE gauge` metadata.

## Example

Statistics payload:

```
statistics	uptime=100;cluster_size=3;
```

Namespace payload:

```
namespace/test	objects=42;client_read_success=5;
```

## Dashboard

The aerospike dashboard for Grafana + Prometheus is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-aerospike.json)

<img alt="Alligator aerospike dashboard" src="/doc/images/dashboard-aerospike.jpg">
