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
