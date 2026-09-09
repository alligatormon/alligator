**Language / Язык:** [English](../../parsers/openclaw.md) | [Русский](openclaw.md)

## OpenClaw

[OpenClaw](https://docs.openclaw.ai/) можно снимать двумя дополняющими способами:

1. **Нативный Prometheus endpoint** (живая диагностика gateway) — OpenClaw отдаёт Prometheus text на `GET /api/diagnostics/prometheus` через плагин `diagnostics-prometheus`. Маршрут требует Gateway operator authentication (`operator.read`). Не публикуйте его без аутентификации. Alligator может scrape'ить этот endpoint через `prometheus_metrics` и отдавать метрики со своего `entrypoint` (с `auth` или без).
2. **Локальный каталог данных** (handler **`openclaw`**) — обходит `~/.openclaw` так же, как [openclaw-exporter](https://github.com/SammyLin/openclaw-exporter): сессии агентов, cron `jobs.json`, расход токенов из session JSONL, размеры markdown в workspace. `filetailer` не читает session files по одному: parser сам ходит по дереву и использует `carg->host` как home для URL `file://`.

Для RPC gateway, токенов/стоимости модели, очереди и liveness лучше нативный endpoint. Parser `openclaw` нужен для состояния на диске, которого нет в gateway metrics (cron jobs, размеры `.md` в workspace, idle/working по логам сессий).

### Нативный Prometheus через Alligator (рекомендуется для live-метрик)

Включите плагин и diagnostics в OpenClaw, затем scrape с Gateway bearer token. `env=` заключайте в кавычки, если в значении заголовка есть пробел. Документация OpenClaw: [Prometheus](https://docs.openclaw.ai/gateway/prometheus).

Минимальный вариант — `entrypoint` **без** `auth` (Prometheus ходит в Alligator свободно; Gateway operator token по-прежнему только на хосте Alligator):

```
aggregate {
    prometheus_metrics http://127.0.0.1:18789/api/diagnostics/prometheus
        'env=Authorization:Bearer ${OPENCLAW_GATEWAY_TOKEN}';
}

entrypoint {
    tcp 1111;
}
```

Опционально: защитить сам Alligator через `auth bearer` / basic (или TLS / `allow`/`deny`). Тогда scraper'ам нужен секрет Alligator, а не Gateway token — см. [entrypoint.md](../entrypoint.md#auth):

```
entrypoint {
    tcp 1111;
    auth bearer alligator-scrape-token;
}
```

Prometheus (или Thanos) ходит в Alligator, а не в Gateway. `auth` на `entrypoint` не обязателен.

Проверка:

```
curl -sS -H 'Authorization: Bearer $OPENCLAW_GATEWAY_TOKEN' \
  http://127.0.0.1:18789/api/diagnostics/prometheus | head
```

Имена метрик берёт OpenClaw (`openclaw_gateway_*`, `openclaw_model_tokens_total`, `openclaw_run_duration_seconds`, …). Alligator их не переименовывает.

### Connection URL (parser `openclaw`)

```
file:///home/user/.openclaw
file://~/.openclaw
```

`~` / `~/...` раскрываются через `$HOME`. Процессу нужно право чтения OpenClaw home (каталоги agents, cron, workspace).

### Пример

```
aggregate {
    openclaw file://~/.openclaw;
}
```

### Metrics (parser `openclaw`)

Агенты находятся в `agents/*`. Workspace — каталоги `workspace` (label `main`) и `workspace-<name>` (label `<name>`).

| Metric | Labels | Meaning |
|--------|--------|---------|
| `openclaw_active_sessions` | — | Файлы сессий `*.jsonl` по всем агентам |
| `openclaw_agent_sessions` | `agent_name` | Файлы сессий на агента |
| `openclaw_agent_state` | `agent_name` | 0=idle, 1=working, 2=thinking, 3=error (по последнему JSONL) |
| `openclaw_agent_last_activity_timestamp_seconds` | `agent_name` | mtime последнего файла сессии |
| `openclaw_cron_jobs_total` | — | Задачи в `cron/jobs.json` |
| `openclaw_cron_jobs_enabled` | — | Включённые задачи |
| `openclaw_cron_job_enabled` | `job_name`, `job_id` | 1/0 (`job_id` обрезается до 8 символов) |
| `openclaw_cron_job_last_run_at_seconds` | `job_name`, `job_id` | Последний запуск |
| `openclaw_cron_job_next_run_at_seconds` | `job_name`, `job_id` | Следующий запуск |
| `openclaw_cron_job_consecutive_errors` | `job_name`, `job_id` | Подряд идущие ошибки |
| `openclaw_cron_job_last_duration_seconds` | `job_name`, `job_id` | Длительность последнего запуска |
| `openclaw_cron_job_last_delivered` | `job_name`, `job_id` | Последнее сообщение доставлено 1/0 |
| `openclaw_cron_job_created_at_seconds` | `job_name`, `job_id` | Время создания |
| `openclaw_cron_session_tokens_last` | `agent`, `cron_name`, `token_type` | Токены последней cron-сессии (`input`/`output`/`cacheRead`/`cacheWrite`) |
| `openclaw_cron_session_cost_last_usd` | `agent`, `cron_name` | Стоимость последней cron-сессии |
| `openclaw_cron_session_total_tokens_last` | `agent`, `cron_name` | Суммарные токены последней cron-сессии |
| `openclaw_agent_session_avg_tokens` | `agent`, `token_type` | Среднее по последним 5 не-cron сессиям |
| `openclaw_agent_session_avg_cost_usd` | `agent` | Средняя стоимость по последним 5 не-cron сессиям |
| `openclaw_agent_session_last_tokens` | `agent`, `token_type` | Последняя не-cron сессия |
| `openclaw_agent_session_last_cost_usd` | `agent` | Стоимость последней не-cron сессии |
| `openclaw_md_file_bytes` | `workspace`, `filename` | Размер markdown-файла |
| `openclaw_md_file_tokens_estimated` | `workspace`, `filename` | `round(bytes / 3.5)` |
| `openclaw_md_workspace_bytes` | `workspace` | Сумма байт markdown |
| `openclaw_md_workspace_tokens_estimated` | `workspace` | Сумма оценочных токенов |

Unit tests: [`src/tests/unit2/parsers.h`](../../src/tests/unit2/parsers.h) (`api_test_parser_openclaw`).
