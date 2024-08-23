
```

Monitoring NameD stats:
named.conf:
```
statistics-channels {
    inet 127.0.0.1 port 8080 allow {any;};
};
```

in zone context add statistics counting:
```
zone "localhost" IN {
        type master;
        file "named.localhost";
        allow-update { none; };
        zone-statistics yes;
};
```

```
aggregate {
	named http://localhost:8080;
}
```
