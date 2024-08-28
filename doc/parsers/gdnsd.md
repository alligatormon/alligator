## gdnsd

To enable the collection of statistics from gdnsd, use the following option:
```
aggregator {
    gdnsd unix:///usr/local/var/run/gdnsd/control.sock;
}
```
