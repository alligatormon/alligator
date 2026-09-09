**Language / Язык:** [English](openclaw.md) | [Русский](../ru/parsers/openclaw.md)

## OpenClaw

[OpenClaw](https://docs.openclaw.ai/) can be scraped in two complementary ways:

1. **Native Prometheus endpoint** (live gateway diagnostics) — OpenClaw exposes Prometheus text at `GET /api/diagnostics/prometheus` via the `diagnostics-prometheus` plugin. The route requires Gateway operator authentication (`operator.read`). Do not publish it unauthenticated. Alligator can scrape that endpoint with `prometheus_metrics` and re-export it from an `entrypoint` (with or without Alligator `auth`).
2. **Local data directory** (handler **`openclaw`**) — walks `~/.openclaw` the same way as [openclaw-exporter](https://github.com/SammyLin/openclaw-exporter): agent sessions, cron `jobs.json`, token usage from session JSONL, workspace markdown sizes. `filetailer` does not crawl session files; the parser walks the tree and uses `carg->host` as the home path for `file://` URLs.

Prefer the native endpoint for gateway RPC, model tokens/cost, queue, and liveness series. Use the `openclaw` parser when you need filesystem state that the gateway metrics do not cover (cron schedule files, workspace `.md` sizes, inferred idle/working from session logs).

### Native Prometheus via Alligator (recommended for live metrics)

Enable the plugin and diagnostics in OpenClaw, then scrape with a Gateway bearer token. Quote `env=` when the header value contains a space. See [OpenClaw Prometheus docs](https://docs.openclaw.ai/gateway/prometheus).

Minimal setup — `entrypoint` without `auth` (Prometheus scrapes Alligator freely; the Gateway operator token still stays only on the Alligator host):

```
aggregate {
    prometheus_metrics http://127.0.0.1:18789/api/diagnostics/prometheus
        'env=Authorization:Bearer ${OPENCLAW_GATEWAY_TOKEN}';
}

entrypoint {
    tcp 1111;
}
```

Optional: protect Alligator itself with `auth bearer` / basic (or TLS / `allow`/`deny`). Scrapers then need the Alligator secret instead of the Gateway token — see [entrypoint.md](../entrypoint.md#auth):

```
entrypoint {
    tcp 1111;
    auth bearer alligator-scrape-token;
}
```

Prometheus (or Thanos) scrapes Alligator, not the Gateway. `auth` on `entrypoint` is optional.

Check:

```
curl -sS -H 'Authorization: Bearer $OPENCLAW_GATEWAY_TOKEN' \
  http://127.0.0.1:18789/api/diagnostics/prometheus | head
```

Metric names come from OpenClaw (`openclaw_gateway_*`, `openclaw_model_tokens_total`, `openclaw_run_duration_seconds`, …). Alligator does not rename them.

### Connection URL (`openclaw` parser)

```
file:///home/user/.openclaw
file://~/.openclaw
```

`~` / `~/...` expand using `$HOME`. The process needs read permission on the OpenClaw home (agents, cron, workspace directories).

### Example

```
aggregate {
    openclaw file://~/.openclaw;
}
```

### Metrics (`openclaw` parser)

Agents are discovered from `agents/*`. Workspaces are discovered from `workspace` (label `main`) and `workspace-<name>` (label `<name>`).

| Metric | Labels | Meaning |
|--------|--------|---------|
| `openclaw_active_sessions` | — | Session `*.jsonl` files across agents |
| `openclaw_agent_sessions` | `agent_name` | Session files per agent |
| `openclaw_agent_state` | `agent_name` | 0=idle, 1=working, 2=thinking, 3=error (from latest JSONL) |
| `openclaw_agent_last_activity_timestamp_seconds` | `agent_name` | mtime of the latest session file |
| `openclaw_cron_jobs_total` | — | Jobs in `cron/jobs.json` |
| `openclaw_cron_jobs_enabled` | — | Enabled jobs |
| `openclaw_cron_job_enabled` | `job_name`, `job_id` | 1/0 (`job_id` truncated to 8 chars) |
| `openclaw_cron_job_last_run_at_seconds` | `job_name`, `job_id` | Last run |
| `openclaw_cron_job_next_run_at_seconds` | `job_name`, `job_id` | Next run |
| `openclaw_cron_job_consecutive_errors` | `job_name`, `job_id` | Consecutive errors |
| `openclaw_cron_job_last_duration_seconds` | `job_name`, `job_id` | Last duration |
| `openclaw_cron_job_last_delivered` | `job_name`, `job_id` | Last message delivered 1/0 |
| `openclaw_cron_job_created_at_seconds` | `job_name`, `job_id` | Created at |
| `openclaw_cron_session_tokens_last` | `agent`, `cron_name`, `token_type` | Last cron session tokens (`input`/`output`/`cacheRead`/`cacheWrite`) |
| `openclaw_cron_session_cost_last_usd` | `agent`, `cron_name` | Last cron session cost |
| `openclaw_cron_session_total_tokens_last` | `agent`, `cron_name` | Last cron session total tokens |
| `openclaw_agent_session_avg_tokens` | `agent`, `token_type` | Average over last 5 non-cron sessions |
| `openclaw_agent_session_avg_cost_usd` | `agent` | Average cost over last 5 non-cron sessions |
| `openclaw_agent_session_last_tokens` | `agent`, `token_type` | Latest non-cron session |
| `openclaw_agent_session_last_cost_usd` | `agent` | Latest non-cron session cost |
| `openclaw_md_file_bytes` | `workspace`, `filename` | Markdown file size |
| `openclaw_md_file_tokens_estimated` | `workspace`, `filename` | `round(bytes / 3.5)` |
| `openclaw_md_workspace_bytes` | `workspace` | Sum of markdown bytes |
| `openclaw_md_workspace_tokens_estimated` | `workspace` | Sum of estimated tokens |

Unit tests: [`src/tests/unit2/parsers.h`](../../src/tests/unit2/parsers.h) (`api_test_parser_openclaw`).
