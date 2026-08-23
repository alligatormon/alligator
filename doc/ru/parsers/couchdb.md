**Language / Язык:** [English](../../parsers/couchdb.md) | [Русский](couchdb.md)

## CouchDB

Собирает статистику CouchDB HTTP API и active tasks.

### Connection URL

```
http://[user:pass@]host[:5984]
```

Порт по умолчанию — **5984**.

### Пример

```
aggregate {
    couchdb http://user:pass@localhost:5984;
}
```

См. [`src/tests/mock/couchdb/alligator.conf`](../../../src/tests/mock/couchdb/alligator.conf).

### Endpoints

Parser читает `/_stats`, `/_config`, `/_active_tasks` и статистику БД из `/_all_dbs`.

### Metrics

- `couchdb_*` — ключи из `/_stats`
- `couchdb_config_*` — значения конфигурации
- `couchdb_active_task_*` — активные задачи

Метки: `type`, `db`, `context`.
