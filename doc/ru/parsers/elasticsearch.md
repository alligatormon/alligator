**Language / Язык:** [English](../../parsers/elasticsearch.md) | [Русский](elasticsearch.md)

## ElasticSearch

Чтобы включить сбор статистики с ES, используйте следующую опцию:
```
aggregate {
    elasticsearch http://localhost:9200;
}
```

Если включена аутентификация, пользователь и пароль можно указать в URL:
```
aggregate {
    elasticsearch http://user:password@localhost:9200;
}
```

Агрегатор опрашивает шесть endpoint'ов и сохраняет префикс метрик `elasticsearch_`:

| Handler | Endpoint |
| --- | --- |
| nodes | `/_nodes/stats` |
| cluster | `/_cluster/stats` |
| health | `/_cluster/health?level=shards` |
| index | `/_stats` |
| settings | `/_settings` |
| cluster settings | `/_cluster/settings?include_defaults=true&flat_settings=true` |

Основные серии (не полный список):

- Здоровье кластера: `elasticsearch_cluster_status{status}`, `elasticsearch_timed_out`, счётчики шардов/индексов
- Статус индекса/шарда: `elasticsearch_indice_status{status}` / `elasticsearch_indice_shard_status{status}` отдают green/yellow/red как `0`/`1`
- Ноды: `elasticsearch_nodes_{total,successful,failed}`, `elasticsearch_node_role{role}`, per-path `elasticsearch_fs_path_{total,free,available}_bytes`
- JVM: `elasticsearch_jvm_*`, `elasticsearch_jvm_gc_collectors{gc,key}`, `elasticsearch_jvm_mem_pools_bytes{pool,key}`
- Disk watermarks: `elasticsearch_disk_watermark_{low,high,flood_stage}_{percent,bytes}`, `elasticsearch_disk_threshold_enabled`
- Статистика индексов: `elasticsearch_shards_{total,successful,failed,skipped}`, `elasticsearch_primaries_*` / `elasticsearch_total_*` с label `key`

Строковые identity-поля из `/_cluster/stats` не экспортируются как gauges. `timestamp`, `max_unsafe_auto_id_timestamp` и `resource_usage_stats` пропускаются (CPU/память хоста уже есть в Alligator `system`).

Alligator также позволяет отправлять метрики в ElasticSearch методами `action`.\
Это даёт возможность задать имя индекса для ElasticSearch с помощью шаблона. Опция поддерживает форматирование strftime, что позволяет динамически менять значения во время выполнения.\
Ниже приведён пример использования. В этом примере метрики будут отправляться в экземпляр ElasticSearch каждые 15 секунд:

```
scheduler {
  name sched-elastic;
  period 15s;
  datasource internal;
  action to-elastic;
}

action {
    name to-elastic;
    expr http://localhost:9200/_bulk;
    serializer elasticsearch;
    index_template alligator-%Y-%m-%d;
}
```

`json-query` также поддерживает разбор JSON-ответов от различных баз данных, включая ElasticSearch.\
Например, это может быть полезно для запроса данных из ElasticSearch и преобразования ответов в метрики:
```
aggregate {
    json_query 'http://localhost:9200/_search?q=something';
}
```

## Dashboard
Системный dashboard для Grafana + Prometheus доступен по следующей [ссылке](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-elasticsearch.json)
<img alt="Dashboard" src="../../images/dashboard-elasticsearch.jpg"><br>
