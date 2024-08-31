## Json Query

To generate metrics from JSON, use the following option:
```
aggregate {
    json_query https://example.com/api/v1/stats;
}
```

### Pquery params
This parameter can be used multiple times. It enables the capability to make queries to parse JSON. If the JSON parser finds a list, it can use the key specified in the brackets [] as a label for the metric.
```
aggregate {
	json_query file:///root//test.json log_level=debug;
	json_query file:///root//test1.json "pquery=.[timestamp].data | .[type]" "pquery=.[timestamp].included.[id]";
}
```
