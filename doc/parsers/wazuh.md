# Wazuh

The `wazuh` aggregate handler reads Wazuh daemon state files from a directory and exports them as Prometheus metrics.

## Configuration

Point the handler at any file inside the Wazuh state directory (typically `/var/ossec/var/run/`):

```
aggregate {
    wazuh file:///var/ossec/var/run/wazuh-agentd.state;
}
```

Alligator reads sibling state files from the same directory:

| File | Metric prefix |
|------|----------------|
| `wazuh-agentd.state` | `wazuh_agentd_*` |
| `wazuh-remoted.state` | `wazuh_remoted_*` |
| `wazuh-analysisd.state` | `wazuh_analysisd_*` |
| `wazuh-logcollector.state` | JSON metrics via `wazuh_logcollector` (see below) |

## State file format

Each line is `name='value'` or `name=value`. Comment lines starting with `#` are ignored.

Value parsing:

- Timestamps in `YYYY-MM-DD HH:MM:SS` form are stored as Unix time.
- Numeric strings are stored as unsigned integers.
- Connection states `connected`, `pending`, and `disconnected` map to `1`, `2`, and `0`.
- Other string values are exported as gauge `1` with a `type` label set to the string value.

## Exported metrics

All metrics from `.state` files use **gauge** type with HELP text `Wazuh API exported metric value.`

Examples:

```
# TYPE wazuh_agentd_status gauge
# HELP wazuh_agentd_status Wazuh API exported metric value.
wazuh_agentd_status 1
wazuh_remoted_queue_size 42
wazuh_analysisd_events_processed 123456
```

Metric names mirror the field names in the state files, prefixed by daemon (`wazuh_agentd_`, `wazuh_remoted_`, `wazuh_analysisd_`).

## Log collector JSON

`wazuh-logcollector.state` is parsed as JSON. Metrics are exported under the `wazuh_logcollector` family using the configured JSON query path (`.global.files.[location]` by default).
