**Language / Язык:** [English](couchdb.md) | [Русский](../ru/parsers/couchdb.md)

## CouchDB

Scrapes CouchDB HTTP API statistics and active tasks.

### Connection URL

```
http://[user:pass@]host[:5984]
```

Default port is **5984**.

### Example

```
aggregate {
    couchdb http://user:pass@localhost:5984;
}
```

See [`src/tests/mock/couchdb/alligator.conf`](../../src/tests/mock/couchdb/alligator.conf).

### Endpoints

The parser reads `/_stats`, `/_config`, `/_active_tasks`, and per-database stats from `/_all_dbs`.

### Metrics

- `couchdb_*` — statistics keys from `/_stats`
- `couchdb_config_*` — configuration values
- `couchdb_active_task_*` — running tasks

Labels include `type`, `db`, and `context` where applicable.
