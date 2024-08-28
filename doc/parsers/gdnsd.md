## gdnsd

To enable the collection of statistics from gdnsd, use the following option:
```
aggregate {
    gdnsd unix:///usr/local/var/run/gdnsd/control.sock;
}
```
