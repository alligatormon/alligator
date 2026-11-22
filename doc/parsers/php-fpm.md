**Language / Язык:** [English](php-fpm.md) | [Русский](../ru/parsers/php-fpm.md)

## PHP-FPM

Handler **`php-fpm`** scrapes the FPM status page in JSON form (`pm.status_path` + `?json`). Prefer an HTTP(S) URL that already reaches the status endpoint (nginx/apache `fastcgi_pass` / `proxy_pass` to the pool).

Enable in the pool config:

```
pm.status_path = /status
```

Collect (URL must request JSON; `?full` is optional for per-state worker counts):

```
aggregate {
    php-fpm http://127.0.0.1/status?json;
    php-fpm http://127.0.0.1/status?json&full;
}
```

Direct FastCGI without a front web server is not implemented; use HTTP to the status location.

### Metrics

| Metric | Type | Notes |
|--------|------|--------|
| `php_fpm_up` | gauge | `1` if the last status scrape parsed (no labels; same series on success and failure) |
| `php_fpm_info{pool,process_manager}` | gauge | Identity (`value=1`) |
| `php_fpm_start_since_seconds` | gauge | Seconds since pool start |
| `php_fpm_accepted_connections_total` | counter | `accepted conn` |
| `php_fpm_listen_queue` | gauge | Current listen queue |
| `php_fpm_max_listen_queue` | gauge | Peak listen queue |
| `php_fpm_listen_queue_length` | gauge | Configured backlog |
| `php_fpm_idle_processes` / `php_fpm_active_processes` / `php_fpm_total_processes` | gauge | Worker pool |
| `php_fpm_max_active_processes` | gauge | Peak active |
| `php_fpm_max_children_reached_total` | counter | Times `pm.max_children` was hit |
| `php_fpm_slow_requests_total` | counter | Slowlog hits |
| `php_fpm_memory_peak_bytes` | gauge | Status-page memory peak (not pool RSS) |
| `php_fpm_process_state{pool,state}` | gauge | Worker counts by state when `?full` is used (no request URI labels); known states are always emitted (incl. `0`) so idle/removed states clear prior scrapes; without `?full`, zeros overwrite prior full-scrape values when `pool` is known |

Pool label is taken from the JSON `pool` field when present.

## Dashboard

The PHP-FPM dashboard for Grafana + Prometheus is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-php-fpm.json)

It is also useful to watch the process and listening sockets:

```
system {
    process php-fpm;
    services php-fpm.service;
}
```
