**Language / Язык:** [English](../../parsers/sentinel.md) | [Русский](sentinel.md)

# Redis Sentinel

Alligator собирает метрики Redis Sentinel из команды `INFO` по протоколу Redis.

## Configuration

```
aggregate {
    sentinel tcp://127.0.0.1:26379;
}
```

Если в URL aggregate настроена аутентификация, коллектор отправляет `AUTH` перед `INFO`.

## Exported metrics

| Metric | Type | Description |
|--------|------|-------------|
| `sentinel_*` | gauge | Числовые поля из вывода Sentinel `INFO`. Ключи, уже имеющие префикс `sentinel_` в источнике, экспортируются как `sentinel_sentinel_*`. |
| `sentinel_status` | gauge | Статус мониторинга master (`ok` или `fail`). |
| `sentinel_slaves` | gauge | Число реплик для мониторируемого master. |
| `sentinel_sentinels` | gauge | Число процессов Sentinel, мониторящих master. |

Метрики с метками используют labels `name` и `address`, разобранные из секции `master0:`.

Все экспортируемые семейства включают метаданные Prometheus `# HELP` и `# TYPE gauge`.

## Example

Фрагмент входных данных:

```
sentinel_masters:1
master0:name=mymaster,status=ok,address=127.0.0.1:26379,slaves=2,sentinels=3
```

Формирует метрики, например:

```
# HELP sentinel_sentinel_masters Redis Sentinel exported metric value.
# TYPE sentinel_sentinel_masters gauge
sentinel_sentinel_masters 1
# HELP sentinel_slaves Redis Sentinel exported metric value.
# TYPE sentinel_slaves gauge
sentinel_slaves{name="mymaster",address="127.0.0.1:26379"} 2
```
