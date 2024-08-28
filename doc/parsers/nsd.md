## Name Server Daemon (NSD)

To enable the collection of statistics from NSD, use the following option:
```
aggregator {
    nsd unix:///run/nsd/nsd.ctl;
}
```
