**Language / Язык:** [English](../../parsers/php-fpm.md) | [Русский](php-fpm.md)

## PHP-FPM

Обработчик **`php-fpm`** снимает status page FPM в JSON (`pm.status_path` + `?json`). Обычно это HTTP(S) URL через nginx/apache до пула.

В конфиге пула:

```
pm.status_path = /status
```

Сбор (в URL должен быть JSON; `?full` опционально для счётчиков воркеров по state):

```
aggregate {
    php-fpm http://127.0.0.1/status?json;
    php-fpm http://127.0.0.1/status?json&full;
}
```

Прямой FastCGI без веб-сервера не реализован — используйте HTTP до status location.

### Метрики

| Метрика | Тип | Примечание |
|---------|-----|------------|
| `php_fpm_up` | gauge | `1`, если последний scrape распарсился (без лейблов; та же серия при успехе и ошибке) |
| `php_fpm_info{pool,process_manager}` | gauge | Идентичность (`value=1`) |
| `php_fpm_start_since_seconds` | gauge | Секунды с старта пула |
| `php_fpm_accepted_connections_total` | counter | `accepted conn` |
| `php_fpm_listen_queue` | gauge | Текущая очередь |
| `php_fpm_max_listen_queue` | gauge | Пик очереди |
| `php_fpm_listen_queue_length` | gauge | Настроенный backlog |
| `php_fpm_idle_processes` / `php_fpm_active_processes` / `php_fpm_total_processes` | gauge | Пул воркеров |
| `php_fpm_max_active_processes` | gauge | Пик active |
| `php_fpm_max_children_reached_total` | counter | Сколько раз упирались в `pm.max_children` |
| `php_fpm_slow_requests_total` | counter | Попадания в slowlog |
| `php_fpm_memory_peak_bytes` | gauge | Peak из status (не RSS пула) |
| `php_fpm_process_state{pool,state}` | gauge | Число воркеров по state при `?full` (без URI); известные state всегда эмитятся (в т.ч. `0`), чтобы сбрасывать устаревшие серии; без `?full` при известном `pool` пишутся нули поверх прошлого full-scrape |

Лейбл `pool` берётся из JSON, если есть.

## Dashboard

Dashboard PHP-FPM для Grafana + Prometheus доступен по [ссылке](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-php-fpm.json)

Также полезно смотреть процесс и сокеты:

```
system {
    process php-fpm;
    services php-fpm.service;
}
```
