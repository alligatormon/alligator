**Language / Язык:** [English](../../parsers/cassandra.md) | [Русский](cassandra.md)

# Cassandra

Alligator поддерживает Cassandra в двух направлениях: **pull** — получение метрик SQL-подобными запросами через aggregate-обработчик `cassandra`, и **push** — отправка метрик в Cassandra через action-сериализатор `cassandra`.

## Pull метрик из Cassandra

Зарегистрируйте datasource в `aggregate`, затем ссылайтесь на него из блоков `query`:

```
aggregate {
    cassandra cassandra://user:password@127.0.0.1:9042 name=from-cassandra;
}

query {
    expr 'SELECT value, name FROM metric';
    field value;
    datasource from-cassandra;
}

query {
    expr "SELECT keyspace_name, table_name, bloom_filter_fp_chance, gc_grace_seconds, compaction FROM system_schema.tables";
    field bloom_filter_fp_chance gc_grace_seconds;
    make cas_db_size;
    datasource from-cassandra;
}
```

Числовые столбцы, перечисленные в `field`, становятся метриками. Остальные столбцы могут использоваться как метки, если они не указаны в `field`.

## Push метрик в Cassandra

Экспортируйте внутренние метрики по расписанию:

```
aggregate {
    cassandra cassandra://user:password@127.0.0.1/app_data name=to-cassandra;
}

scheduler {
    name cassandra;
    period 15s;
    datasource internal;
    action to-cassandra;
}

action {
    name to-cassandra;
    expr cassandra;
    serializer cassandra;
}
```

Сериализатор `cassandra` сохраняет временные ряды в Cassandra. Используйте `index_template` в action, если нужны имена таблиц или ключей с учётом времени (поддерживается форматирование strftime).

## Graphite и file exporters

Сама Cassandra может экспортировать метрики через Graphite или JMX/file exporters. Эти потоки можно собирать обычными парсерами Alligator (`prometheus_metrics`, пользовательские скрипты `exec://` и т. д.) без SQL-обработчика `cassandra`.
