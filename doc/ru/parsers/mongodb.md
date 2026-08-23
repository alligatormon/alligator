**Language / Язык:** [English](../../parsers/mongodb.md) | [Русский](mongodb.md)

## MongoDB

Парсер MongoDB поддерживает выполнение пользовательских запросов к обнаруженным базам данных и коллекциям.

### Базовая настройка

```conf
aggregate {
  mongodb "mongodb://user:password@localhost:27017/admin" name=mongo_main log_level=off;
}
```


### Сопоставление datasource запросов

Для каждой обнаруженной пары `<db>/<collection>` парсер пробует следующие datasource:

1. `name`
2. `name/*`
3. `name/<db>`
4. `name/<db>/<collection>`

Так, для `name=mongo_main`, `db=application`, `collection=users`, могут подойти все из перечисленного:

- `mongo_main`
- `mongo_main/*`
- `mongo_main/appliaction`
- `mongo_main/application/users`

### Формат выражения query

`query.expr` поддерживает:

- find с указанием коллекции:
  - `db.<collection>.find({...})`
- простой JSON-фильтр:
  - `{"enabled":true}`

Примеры:

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

### Режимы обработки результатов

Результат Mongo-запроса можно обработать двумя способами.

#### 1) Режим `field` (поведение по умолчанию, SQL-подобное)

- Настройте одно или несколько значений `field`.
- Числовые поля из возвращённых документов становятся значениями метрик.
- Строковые поля становятся метками.

```conf
query {
  expr "db.orders.find({\"status\":\"open\"})";
  datasource mongo_main/application/orders;
  make mongo_open_orders;
  field amount;
  field count;
}
```

#### 2) Режим `json_query` (`jpath`)

- Настройте одну или несколько директив `jpath`.
- Каждый возвращённый документ передаётся в `json_query(...)` с этими путями.
- Если присутствует `jpath`, парсер использует для этого узла query режим `json_query`.

```conf
query {
  expr "db.users.find({\"enabled\":true})";
  datasource mongo_main/application/users;
  make mongo_users_json;
  jpath ".logins";
  jpath ".profile.age";
}
```

### Логирование и диагностика

При `log_level=trace` можно увидеть:

- ключи datasource, проверяемые через `query_get`
- результат совпадения/несовпадения `query_get`
- сырые документы, возвращённые Mongo
- вызов разбора `json_query` для режима `jpath`
- сводку выполнения запроса (`docs=...`)

Если видны только логи `listDatabases/listCollections` и нет выполнения запросов:

- проверьте, что `query.datasource` совпадает с обнаруженными именами DB/collection
- проверьте имя коллекции в `db.<collection>.find(...)`
- проверьте, что `field` указывает на числовое поле (для режима `field`)
