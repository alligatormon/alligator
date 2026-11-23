**Language / Язык:** [English](../../parsers/flower.md) | [Русский](flower.md)

## Flower (Celery)

Собирает счётчики задач и воркеров из HTML dashboard Celery Flower.

### Connection URL

```
http://host[:5555]
```

Порт Flower по умолчанию — **5555**. HTTP basic auth поддерживается в URL.

### Пример

```
aggregate {
    flower http://localhost:5555;
}
```

См. [`src/tests/system/flower/alligator.yaml`](../../../src/tests/system/flower/alligator.yaml).

### Metrics

Итоги кластера:

- `flower_cluster_tasks_active` (gauge)
- `flower_cluster_tasks_{processed,failed,succeeded,retried}_total` (counters)

По воркеру:

- `flower_worker_up`
- `flower_tasks_active{worker=…}` (gauge)
- `flower_tasks_{processed,failed,succeeded,retried}_total{worker=…}` (counters)
