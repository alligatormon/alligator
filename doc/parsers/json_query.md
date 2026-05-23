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

### jq-style pipeline support
`pquery` now supports jq-like stage chaining and branching:
- `|` moves to the next stage.
- `,` creates branches in the same stage.
- `.field` moves deeper in JSON.
- `[field]` extracts a label from the current JSON object (root object or each array element).
- `[field:label]` (or `[field=label]`) extracts a field and renames the label.
- Label blocks support spaces or commas between items.

For line-delimited JSON objects (JSON Lines / NDJSON, for example PostgreSQL `log_destination = jsonlog`), use a root label block. When a buffer is not valid as a single JSON document, `json_query` parses it line by line (one object per line):

```
aggregate {
    json_query file:///var/log/postgresql/json.log \
        "pquery=.[message dbname user error_severity state_code ps backend_type session_id remote_host timestamp]";
}
```

Each non-empty line must be a complete JSON object. With file tailing, a read chunk can split a line in the middle; that fragment is skipped until the next read provides a full line (same limitation as other stream parsers without line buffering).

Examples:
```
aggregate {
	# Old style, still valid:
	json_query file:///root/test1.json "pquery=.[timestamp].included.[id]";

	# Two branches in one stage:
	json_query file:///root/test1.json "pquery=.[timestamp] | .data, .included | .[id,type]";

	# Label aliases:
	json_query file:///root/test1.json "pquery=.[timestamp:ts] | .included | .[id:entity_id type:kind]";

	# Multiple pquery are merged:
	json_query file:///root/test1.json \
		"pquery=.[timestamp] | .data | .[type]" \
		"pquery=.[timestamp] | .included | .[id]";
}
```
