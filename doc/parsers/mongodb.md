## MongoDB

MongoDB parser supports custom query execution against discovered databases and collections.

### Basic setup

```conf
aggregate {
  mongodb "mongodb://user:password@localhost:27017/admin" name=mongo_main log_level=off;
}
```


### Query datasource matching

For each discovered `<db>/<collection>`, parser tries these datasources:

1. `name`
2. `name/*`
3. `name/<db>`
4. `name/<db>/<collection>`

So for `name=mongo_main`, `db=application`, `collection=users`, all of these can match:

- `mongo_main`
- `mongo_main/*`
- `mongo_main/appliaction`
- `mongo_main/application/users`

### Query expression format

`query.expr` supports:

- collection-scoped find:
  - `db.<collection>.find({...})`
- plain JSON filter:
  - `{"enabled":true}`

Examples:

```conf
query {
  expr "db.users.find({\"enabled\":true})";
  datasource mongo_main/application/users;
  make mongo_users_logins;
  field logins;
}
```

```conf
query {
  expr "{\"enabled\":true}";
  datasource mongo_main/application;
  make mongo_enabled_docs;
  field count;
}
```

### Result processing modes

Mongo query result can be processed in two ways.

#### 1) `field` mode (default SQL-like behavior)

- Configure one or more `field` values.
- Numeric fields from returned documents become metric values.
- String fields become labels.

```conf
query {
  expr "db.orders.find({\"status\":\"open\"})";
  datasource mongo_main/application/orders;
  make mongo_open_orders;
  field amount;
  field count;
}
```

#### 2) `json_query` mode (`jpath`)

- Configure one or more `jpath` directives.
- Each returned document is passed through `json_query(...)` with these paths.
- If `jpath` is present, parser uses `json_query` mode for this query node.

```conf
query {
  expr "db.users.find({\"enabled\":true})";
  datasource mongo_main/application/users;
  make mongo_users_json;
  jpath ".logins";
  jpath ".profile.age";
}
```

### Logging and troubleshooting

With `log_level=trace` you can see:

- datasource keys being checked via `query_get`
- `query_get` match/miss result
- raw Mongo returned documents
- `json_query` parsing invocation for `jpath` mode
- query execution summary (`docs=...`)

If you only see `listDatabases/listCollections` logs and no query execution:

- verify `query.datasource` matches discovered DB/collection names
- verify collection name in `db.<collection>.find(...)`
- verify `field` points to numeric field (for `field` mode)
