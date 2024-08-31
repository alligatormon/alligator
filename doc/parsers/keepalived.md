## Keepalived

To enable the collection of statistics from Keepalived, use the following option:
```
aggregate {
    keepalived file:///var/run/keepalived.pid state=begin notify=true;
}
```

### To collect statistics
In addition to the common method of checking Keepalived, it also supports notifications about state changes. To configure this, specify the following configuration in the `vrrp_instance` context:
```
notify /usr/libexec/keepalived-notify.sh root
```

Below is an example script to add metrics to Alligator:
```
#!/bin/sh
umask -S u=rwx,g=rx,o=rx
echo "[$(date -Iseconds)]" "$0" "$@" >>"/var/run/keepalived.$1.$2.state"
echo "keepalive_last_time{type=\"$1\", name=\"$2\", state=\"$3\"} `date +%s`" > /var/run/keepalived_time_state
echo "keepalive_last_status{type=\"$1\", name=\"$2\", state=\"$3\"} 1" >> /var/run/keepalived_time_state
```

Finally, to enable Alligator to check this file, it is necessary to specify the file in the Alligator configuration:
```
aggregate {
    prometheus_metrics file:///var/run/keepalived_time_state state=save notify=true;
}
```

It is also useful to check process statistics, running services:
```
system {
    process keepalived;
    services keepalived.service;
}
```
