**Language / Язык:** [English](flower.md) | [Русский](../ru/parsers/flower.md)

## Flower (Celery)

Scrapes Celery Flower monitoring dashboard HTML for task and worker counters.

### Connection URL

```
http://host[:5555]
```

Default Flower port is **5555**. HTTP basic auth is supported in the URL.

### Example

```
aggregate {
    flower http://localhost:5555;
}
```

See [`src/tests/system/flower/alligator.yaml`](../../src/tests/system/flower/alligator.yaml).

### Metrics

Cluster totals:

- `flower_tasks_total_active`, `flower_tasks_total_processed`, `flower_tasks_total_failed`, `flower_tasks_total_succeeded`, `flower_tasks_total_retried`

Per worker:

- `flower_worker_up`
- `flower_tasks_{active,processed,failed,succeeded,retried}{worker=…}`
