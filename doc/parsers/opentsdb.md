## OpenTSDB

To enable the collection of statistics from OpenTSDB, use the following option:
```
aggregate {
    opentsdb http://localhost:4242/api/stats;
}
```
