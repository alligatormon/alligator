# Varnish

Varnish can be monitored using [varnishstat](https://varnish-cache.org/docs/trunk/reference/varnishstat.html).

In Alligator, the stats are collected using the following configuration:
```
aggregate {
    varnish 'exec:///usr/bin/varnishstat -j';
}
```
