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

- `flower_tasks_total_active`, `flower_tasks_total_processed`, `flower_tasks_total_failed`, `flower_tasks_total_successed`, `flower_tasks_total_retried`

По воркеру:

- `flower_worker_status`
- `flower_tasks_{active,processed,failed,successed,retried}`
