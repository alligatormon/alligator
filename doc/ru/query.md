**Language / Язык:** [English](../query.md) | [Русский](query.md)

# Query
Контекст query создаёт метрики на основе запроса. Query может выполняться к локальному хранилищу метрик Alligator или к внешней базе данных, зарегистрированной в `aggregate`.

## query_period
По умолчанию: server default\
Множественное число: нет

Задаёт, как часто Alligator повторно выполняет все блоки `query`. Используйте те же единицы времени, что и в остальной конфигурации (`s`, `ms`, `h` и т. д.).

```
query_period 20s;
```

Это отделено от `period` отдельного aggregate (который управляет опросом внешних target).

## make
Задаёт имя создаваемой метрики для internal query и служит ключом для операций API.


## expr
Запрос, относящийся к указанной базе данных.\
Например, для internal хранилища метрик Alligator это может быть promql. Либо это может быть [SQL](https://github.com/alligatormon/alligator/blob/master/doc/parsers/postgresql.md)-запрос.




Примечание: для query с datasource `internal` `count()` использует синтаксис PromQL и возвращает число совпавших time series.
В отличие от поведения PromQL, эта реализация всё равно возвращает результат, даже если ни одна метрика не совпала с фильтром.
Используйте `count by (<label>, ...) (...)`, чтобы сохранить группирующие метки в выводе вместо одного итогового значения.

Голый идентификатор метрики — это точное имя. Чтобы сопоставить семейство имён, используйте `{__name__=~"..."}` или `{__name__!~"..."}` (PCRE, без якоря). `name` — псевдоним `__name__`. Некорректное регулярное выражение не совпадёт ни с чем; метки `=~` для ключей, отличных от `__name__`, игнорируются.

```
query {
	expr 'count({__name__=~"^socket_stat"})';
	make socket_stat_series;
	datasource internal;
}
```


## action
Задаёт контекст action для запуска при срабатывании expr. Работает только для datasource `internal`.\
[Описание](https://github.com/alligatormon/alligator/blob/master/doc/action.md) контекста action.


## datasource
Указывает источник данных для query.\
Для локальных запросов метрик используйте ключ `internal`.\
Для внешних баз данных поле `name` должно быть указано в контексте aggregator вместе с базой; это имя будет служить datasource для query.


## ns
Когда у базы данных есть internal namespace (например, базы в реляционной СУБД), это поле задаёт имя такого namespace.


## field
Используется для выбора столбца в SQL-ответах. Имя столбца будет использовано как имя метрики, а значения столбца — как значения метрики.


## except
Пропускает базы по имени, когда `datasource` — wildcard (`pg/*`, `mysql/*` и аналоги). Также отбрасывает строки результата, у которых label имени базы (`dbname`, `datname` или `psql_database`) совпал, в том числе для datasource без wildcard (`pg`), если SQL возвращает по строке на базу. Имена без обёртки — точное совпадение (hashtable). Токены в `/…/` — PCRE (то же соглашение, что `system { process …; }`), без якоря, если в шаблоне нет `^`/`$`. Пустой `except` ничего не пропускает. Сопоставление идёт по имени базы (или keyspace), не по полному ключу datasource. Системные базы вроде `template0` не пропускаются, пока их нет в `except`.

Если несколько блоков query делят один wildcard datasource с разными списками `except`, база пропускается только для тех query, которые её исключают. Подключение к базе не создаётся, только если все query этого datasource её except'ят.

```
query {
    expr "
        WITH s AS (
            SELECT *
            FROM pg_stat_statements(false)
            WHERE calls > 0
              AND dbid = (SELECT oid FROM pg_database WHERE datname = current_database())
        ),
        topq AS (
            SELECT queryid
            FROM s
            ORDER BY total_exec_time DESC
            LIMIT 50
        )
        SELECT
            current_database() AS psql_database,
            s.queryid::text AS psql_queryid,
            round(sum(s.total_exec_time)::numeric, 3)::double precision AS postgresql_pgss_top_total_ms
        FROM s
        JOIN topq t USING (queryid)
        GROUP BY 1, 2;
    ";
    field postgresql_pgss_top_total_ms;
    datasource pg/*;
    except db1 db2 /db[0-9]/;
    make postgresql_pgss_top_total_ms;
}
```

Форма JSON: `"except": ["db1", "db2", "/db[0-9]/"]`.


## Примеры

### Internal query Alligator

Эта конфигурация проверит наличие процесса `dockerd` на портах 8085 и 8080 и создаст метрику `socket_match` как результат query:
```
query_period 20s;
query {
	expr 'count by (src_port, process) (socket_stat{process="dockerd", src_port="8085"})';
	make socket_match;
	datasource internal;
}
query {
	expr 'count by (src_port, process) (socket_stat{process="dockerd", src_port="8080"})';
	make socket_match;
	datasource internal;
}
```

### Query к внешним базам данных

Эта конфигурация собирает метрики о размерах баз данных с внешнего экземпляра MySQL:
```
aggregate {
	mysql mysql://user:password@127.0.0.1:3306 name=mysql;
}

query {
	expr 'SELECT table_schema "db_name", table_name "table", ROUND(SUM(data_length), 1) "mysql_table_size", ROUND(SUM(index_length), 1) "mysql_index_size", table_rows "mysql_table_rows" FROM information_schema.tables  GROUP BY table_schema';
	field mysql_table_size mysql_index_size mysql_table_rows;
	make mysql_db_size;
	datasource mysql;
}
```
