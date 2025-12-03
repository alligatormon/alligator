## syslog-ng

To enable the collection of statistics from syslog-ng, use the following option:
```
aggregate {
    syslog-ng unix:///var/lib/syslog-ng/syslog-ng.ctl;
}
```

## Dashboard
The syslog-ng dashboard for Grafana + Prometheus is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-syslog-ng.json)
<img alt="Alligator syslog-ng dashboard" src="/doc/images/dashboard-syslog-ng.jpg">
