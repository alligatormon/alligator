## syslog-ng

To enable the collection of statistics from syslog-ng, use the following option:
```
aggregate {
    syslog-ng unix:///var/lib/syslog-ng/syslog-ng.ctl;
}
```