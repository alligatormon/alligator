## OpenMetrics

To enable the collection of statistics from any endpoints with openmetrics format, use the following option:
```
aggregate {
    prometheus_metrics http://localhost/metrics;
}
```

### Example of usage reading metrics from a file:
```
aggregate {
    prometheus_metrics file:///var/run/keepalived_time_state state=save notify=true;
}
```

This option make it possible to collect metrics from other exporters.
