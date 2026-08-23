**Language / Язык:** [English](../../parsers/mysql.md) | [Русский](mysql.md)

# MySQL, MariaDB

Выполняет пользовательский SQL по протоколу MySQL (`mysql://`) и превращает выбранные колонки в метрики через контекст `query`. Также подходит для SQL-движков с протоколом MySQL (Manticore, Sphinx, ProxySQL), если запрос совместим.

### Connection URL

```
mysql://[user:password@]host[:3306]
```

Задайте `name=` у aggregate, чтобы `query` ссылался на него как `datasource`.

### Пример

```
aggregate {
    mysql mysql://user:password@localhost name=mysql;
}

query {
    expr "SELECT table_schema \"db_name\", ROUND(SUM(data_length + index_length), 1) \"mysql_database_size\" FROM information_schema.tables GROUP BY table_schema";
    field mysql_database_size;
    datasource mysql;
    make mysql_db_size;
}
```

### Metrics

Имена метрик берутся из `make` / `field` в `query` (например `mysql_db_size` / `mysql_database_size`). См. [query.md](../query.md).
