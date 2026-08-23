**Language / Язык:** [English](../cluster.md) | [Русский](cluster.md)

# Cluster
Alligator поддерживает два типа конфигурации кластера: для приёма метрик и для сбора метрик.
<br>
<p align="center">
<h1 align="center" style="border-bottom: none">
    <img alt="alligator-cluster-entrypoint" src="../images/cluster-entr.jpeg"></a><br>
</h1>
<br>
Если кластер включён в контексте aggregate, становится доступна lock-based кластеризация.
<br>
<h1 align="center" style="border-bottom: none">
    <img alt="alligator-cluster-aggregate" src="../images/cluster-aggr.jpeg"></a><br>
</h1>

<br>
<br>
</p>

Cluster включает многонодовые возможности и может использоваться в двух направлениях — как приёмник метрик или как crawler метрик.\
Чтобы настроить кластер-приёмник метрик, каждая нода должна быть сконфигурирована в entrypoint так:
```
entrypoint {
    tcp 1111;
    cluster repl;
    instance srv1.example.com:1111;
}
```

Чтобы настроить crawler метрик с эксклюзивной блокировкой, каждая нода должна сконфигурировать контекст aggregate так:
```
aggregate {
    elasticsearch http://localhost:9200/ cluster=repl instance=srv1.example.com:1111;
}
```


## name
Задаёт имя кластера, которое можно использовать как ссылку в aggregate, entrypoint или в API.


## type
Доступные значения:
- oplog
- sharedlock

Значение sharedlock можно использовать в контексте aggregate. Это позволяет выполнять исходящие запросы ровно один раз через кластер.
Oplog — вариант, который позволяет принимать и распределять метрики между нодами кластера.


## server
Задаёт все хосты кластера.


## size
По умолчанию: 1000
Задаёт размер oplog. При переполнении operation log старые метрики в oplog будут отброшены.


## shardinag\_key
По умолчанию: `__name__`

Задаёт ключ шардирования для распределения ключей по нодам кластера.


## replica\_factor
Повышает доступность метрик, если часть нод выходит из строя.


# Пример
```
cluster {
    name repl;
    size 10000;
    sharding_key __name__;
    replica_factor 2;
    type oplog;
    servers  srv1.example.com:1111 srv2.example.com:1111 srv3.example.com:1111;
}
```
