**Language / Язык:** [English](../../vrl/README.md) | [Русский](README.md)

# VRL (avrl) в alligator

avrl — C-реализация Vector Remap Language. В alligator он
подключается как **отдельная статическая библиотека** (`avrl_static`) со слоем интеграции в `src/vrl/`.

## Разрешение исходников (лучше, чем плоский listing amtail)

CMake ищет avrl в таком порядке:

1. `src/external/avrl/` — git submodule (предпочтительно для релизов)
2. `../avrl/` — соседний checkout (локальная разработка)

Текущая ветка использует git submodule `src/external/avrl`.
`depbuild.sh` rsync-ит его на удалённый builder (тот же паттерн, что и amtail).

## Конфигурация

```
vrl {
    name app_logs;
    script /etc/vrl/app.vrl;
    # или: program ".status = upcase(.message)";
}

aggregate {
    vrl file:///var/log/app.log name=app_logs
        start_pattern=^\S
        condition_pattern=^\s
        multiline_mode=continue_through
        log_level=info;
}

entrypoint {
    bind 0.0.0.0:19191;
    handler vrl;
    vrl app_logs;
}
```

### Multiline (совместимо с Vector, общее для mtail и grok)

Тот же assembler, что у file sources Vector (`start_pattern` + `condition_pattern` + `mode`).
Настраивается на **aggregate** (применяется к `mtail`, `grok` и `vrl`) или в
JSON программы / объекте `multiline`.

| Поле | Значение |
|-------|---------|
| `start_pattern` | Regex; совпадающая строка начинает новое логическое сообщение |
| `condition_pattern` | Regex, интерпретируемый согласно `multiline_mode` |
| `multiline_mode` | `continue_through` \| `continue_past` \| `halt_before` \| `halt_with` |

Пример (stack trace в стиле Java — продолжающиеся строки с пробелом):

```
aggregate {
    mtail file:///var/log/app.log name=app
        start_pattern=^\S
        condition_pattern=^\s
        multiline_mode=continue_through;
}
```

В программе `vrl` (JSON API):

```json
"multiline": {
  "start_pattern": "^\\S",
  "condition_pattern": "^\\s",
  "mode": "continue_through"
}
```

Устаревшая форма с одним паттерном по-прежнему работает: `{ "mode": "halt_before", "pattern": "^\\S" }`
(паттерн используется и для start, и для condition).

Ключ JSON API: `"vrl": [ { "name": "...", "script"|"program": "...", "multiline": {...}, "dns_timeout": "2s", "dns_negative_ttl": "30s" } ]`

### Асинхронный DNS: `dns_lookup` / `reverse_dns`

> **Host builtins (клей alligator).** Они не входят в саму библиотеку avrl;
> alligator регистрирует их в `src/vrl/vrl_dns.c` и управляет suspend/resume из
> `src/vrl/vrl.c`. Они используют общий кэш resolver alligator (`resolver_cache_lookup`).

Обе функции **полностью асинхронны**: разрешение выполняется через libuv
(`uv_getaddrinfo` / `uv_getnameinfo`) и **не** блокирует остальную работу alligator
(другие aggregate, scrape, entrypoint, таймеры). Пока поток ждёт,
alligator продолжает обслуживать остальной процесс.

При **cache miss** builtin запускает одноразовое async-разрешение, возвращает
`null` и приостанавливает **этот** поток. `vrl_run_record()` буферизует запись, а
poll timer воспроизводит её, когда ответ получен (или после `dns_timeout` — один раз
с `null` через `dns_force_null`). Дальнейшие записи из того же источника ставятся в
очередь до этого момента, поэтому **строки логов для этого потока обрабатываются
только после полного завершения разрешения или попадания в (positive/negative) кэш** —
порядок сохраняется, но пропускная способность файла может проседать на cold lookups.

**`reverse_dns` vs `dns_lookup` в log pipelines:** PTR-запросы часто занимают
**гораздо больше wall-clock времени на потоке**, чем forward `dns_lookup`.
Client/peer IP в логах сильно вариативны (много разных адресов, низкое
переиспользование), поэтому positive cache помогает меньше, и каждый новый IP — miss
(suspend + resolve, или окно negative cache, если включено). Forward lookups небольшого
набора hostname быстро прогревают кэш. Предпочитайте negative cache
и/или избегайте безусловного `reverse_dns` на полях IP с высокой кардинальностью, когда
важна задержка.

| Поле | Alias | По умолчанию | Значение |
|-------|-------|---------|---------|
| `dns_timeout` | `dns_timeout_ms` | `2s` (2000 ms) | Сколько запись может ждать first-sight разрешения до timeout path |
| `dns_poll` | `dns_poll_ms` | `50ms` | Как часто resume timer перепроверяет кэш |
| `dns_negative_ttl` | `dns_negative_ttl_ms` | `0` (выкл.) | Opt-in TTL negative cache (см. ниже) |
| `dns_negative_cache_max` | — | `100000` | Лимит distinct bad names |

**Значения длительности** используют те же правила, что и другие time fields alligator
([configuration.md](../configuration.md#доступные-единицы-времени-в-файле-конфигурации)):

- JSON/plain **string** с единицами: `ms`, `s`, `m`, `h`, `d`, `w` (например, `"2s"`, `"2000ms"`, `"30s"`). Голое число в строке трактуется как **секунды**.
- JSON **integer** / **real**: миллисекунды (так `2000` == `"2000ms"`).
- Plain config всегда даёт строки, поэтому предпочитайте явную единицу: `dns_timeout 2s;`, а не голое число.

### Асинхронный HTTP: `http_request`

> **Host builtin (клей alligator)** в `src/vrl/vrl_http.c`. Та же модель suspend/resume,
> что у DNS: cache miss → oneshot GET → pause stream → replay с результатом.

```
resp = http_request(.referer)   # null пока pending / при ошибке
# resp.body (string), resp.status (int)
obj, err = parse_json(resp.body)
```

Под капотом использует `aggregator_oneshot`. IP-literal хосты seed-ятся в
resolver cache, чтобы локальные URL вроде `http://127.0.0.1:8765/...` не зависали.
Deadline паузы — минимум 10s (или `dns_timeout`, если больше). Переиспользует
`dns_timeout` / `dns_poll` для await timer.

Метрики времени (system namespace, та же идея, что у DNS) — считаются **один раз на
вызов `http_request()`, вернувший финальный ответ** (включая warm cache hits).
Network awaits учитывают wall-clock wait; чистые cache hits — duration 0:

- `vrl_http_requests_total{result="success|failure|timeout", method="GET"}`
- `vrl_http_request_duration_seconds_sum{result=..., method="GET"}`

Средняя задержка = `duration_sum / requests_total` для данного `result`.
Per-line script metrics (например, `apache_referer_json_total`) остаются отдельными.

### Enrichment tables (host)

Объявите именованные таблицы в конфиге, затем вызывайте Vector-совместимые builtins:

```
enrichment_table {
    name codes;
    type file;                    # CSV (требуется строка заголовка)
    path /etc/alligator/codes.csv;
}

enrichment_table {
    name city;
    type mmdb;                    # или geoip
    path /usr/share/GeoIP/GeoLite2-City.mmdb;
}
```

```
row, err = get_enrichment_table_record("codes", {"code": .code})
rows = find_enrichment_table_records("codes", {"code": .code})
geo, err = get_enrichment_table_record("city", {"ip": .client_ip})
```

- **file**: exact-match по всем полям condition object; `get_*` ошибается при 0 или >1 строках.
- **mmdb/geoip**: condition должен включать `"ip"`; возвращает Vector-подобные поля (`country_code`,
  `city_name`, `latitude`, …), если они есть. Требует Conan/system `libmaxminddb`
  (жёсткая зависимость).

### Secrets и semantic meaning (host)

Per-event maps на VRL-потоке (очищаются между записями):

```
set_secret("token", "s3cr3t")
tok = get_secret("token")
remove_secret("token")
set_semantic_meaning(".status", "http.status_code")   # string path (optional leading '.')
```

#### Negative DNS cache (opt-in)

**Какую проблему решает:** раньше имя, которое failed или timed out, никогда не
запоминалось, и каждая следующая строка лога с этим bad domain запускала новое
async-разрешение и полный suspend pipeline на `dns_timeout`. С включённым negative cache
failures/timeouts мemoize-ятся на настраиваемое окно.

**Порядок lookup** в `vrl_dns_common()`:

1. `dns_force_null` one-shot (timeout replay) — без изменений
2. Positive cache (`resolver_cache_lookup`) — реальный ответ всегда побеждает
3. Negative cache — если live entry существует, сразу `null` (без suspend, без re-resolve)
4. Cache miss → suspend + async resolution

Negative entry записывается когда:

- `getaddrinfo` / `getnameinfo` возвращает ошибку или пустой результат (resolver callbacks в `vrl_dns.c`)
- приостановленный поток timed out в ожидании (`vrl_resume_timer_cb` в `vrl.c`)

Когда TTL entry истекает, имя re-resolve-ится. Короткий TTL — bad domains раньше;
длинный — реже retry для persistently-bad names.

**Plain config:**

```
vrl {
    name postfix;
    script /etc/vrl/postfix.vrl;
    dns_timeout 2s;
    dns_negative_ttl 30s;
    dns_negative_cache_max 50000;
}
```

**JSON API:**

```json
{
  "name": "postfix",
  "script": "/etc/vrl/postfix.vrl",
  "dns_timeout": "2s",
  "dns_negative_ttl": "30s",
  "dns_negative_cache_max": 50000
}
```

Integer milliseconds по-прежнему работают (`"dns_timeout_ms": 2000`, `"dns_negative_ttl_ms": 30000`).

- `dns_negative_ttl` — включить и задать negative TTL. `0` / omitted отключает
  и сохраняет старое поведение suspend-each-time.
- `dns_negative_cache_max` — max distinct remembered names (по умолчанию `100000`) для
  ограничения памяти. При переполнении новые bad names fallback на suspend-each-time.

**Заметки по реализации**

- Process-global `alligator_ht` с ключом `"<name>:<rrtype>"`, хранит absolute
  `expire_ms`.
- Защищено своим `uv_mutex_t`: search-then-evict/refresh на loop
  thread (callbacks/timer) и во время VRL record processing; internal rwlock `alligator_ht`
  защищает только отдельные ops, не compound sequence.
- Expired entries evict-ятся lazily при access; insertions останавливаются на cap.
- Positive precedence сохранён: если async resolution успешен после
  timeout, positive cache entry берёт верх, stale negative entry игнорируется
  (и eventually evict-ится).

Metrics (system carg): `vrl_dns_resolutions_total` и
`vrl_dns_resolution_duration_seconds_sum` с labels `type` (`A`/`AAAA`/`PTR`)
и `result` (`success`/`failure`/`timeout`).

## Отладка

На строке aggregate задайте `log_level=info` (или `debug`):

```
aggregate {
    vrl file:///var/log/app.log name=app log_level=info;
}
```

Также запускайте alligator с `-l 3` (или выше), чтобы видеть global logs.

Вы должны видеть строки вроде:

```
vrl: handler got N bytes (name=app)
vrl: run record (...) program='app': 'hello...'
vrl: metric_add lines_total = 1
vrl: processed chunk via linebuf (multiline=on)
```

Если после append в лог **ничего** не появляется:

1. Alligator действительно читает файл? Проверьте aggregate `state=stream` / filetailer path.
2. Предпочитайте **append после start**: `echo hello >> /var/log/app.log`
3. Строкам нужен trailing `\n`.
4. Программа должна быть зарегистрирована под тем же `name=`, что и aggregate.
5. Предпочитайте **script file** вместо inline `program` (quoting в plain config легко сломать):

```
# /etc/vrl/app.vrl
.metric = { "name": "lines_total", "value": 1 }

# alligator.conf
vrl { name app; script /etc/vrl/app.vrl; }
aggregate { vrl file:///var/log/app.log name=app log_level=info; }
```

С `log_level=debug` вы также получите полный JSON event после transform.

### Экспорт метрик: `.metric` / `.metrics`

> **Отличие от Vector VRL.** Стандартный Vector Remap Language не имеет metric
> export objects, Prometheus histogram observation и полей `update` /
> `buckets`. `.metric` / `.metrics` (и форма histogram ниже) —
> **расширение Alligator** поверх avrl: скрипт remaps event, затем
> Alligator читает эти поля и пушит series в metric tree.

Каждому object нужны `name` (string) и `value` (number/bool). Необязательно:

| Поле | Значение |
|-------|---------|
| `labels` | object строковых labels |
| `update` | если `true`, вызывается `metric_update` (increment) вместо `metric_add` (set) |
| `type` | `counter` / `gauge` / `histogram` / `summary` — задаёт Prometheus `# TYPE` |
| `buckets` | array numeric upper bounds — **histogram observation** (только Alligator) |

#### Histogram observation (`buckets`)

Когда `buckets` — непустой number array (и `type` omitted или
`"histogram"`), Alligator трактует `value` как один sample и обновляет:

- `{name}_bucket{le="…"}` — cumulative counts для каждого bound `value <= bound`, плюс `le="+Inf"`
- `{name}_sum` — добавляет `value`
- `{name}_count` — добавляет `1`

```
.metrics = [{
  "name": "postgresql_query_duration_ms",
  "value": .ms,
  "type": "histogram",
  "buckets": [1, 5, 10, 25, 50, 100, 250, 500, 1000, 5000],
  "labels": {
    "database": .database_name,
    "duration_kind": .duration_kind
  }
}]
```

Та же идея, что mtail `histogram … buckets …`, не feature Vector VRL.
Можно по-прежнему emit-ить raw `_bucket` / `_sum` / `_count` series сами с
`"update": true`, если нужен полный контроль.

### Экспорт логов: `.log` / `.logs` → `log_channel_out`

> **Отличие от Vector VRL.** Shipping remapped events в sink не входит
> в standard VRL. Alligator emit-ит логи только когда скрипт явно задаёт
> `.log` или `.logs`, и у aggregate/entrypoint задан `log_channel_out`.
> Metrics (`.metrics`) и logs независимы — оба могут сработать на одной record.

| Поле | Значение |
|-------|---------|
| `.log` | одна string (plain body) или один **flat object** (JSON document) |
| `.logs` | array strings и/или flat objects |

Objects пишутся как **flat** JSON documents (не nested под `message`).
Strings идут через `log_channel_write_raw` (json/elastic channels оборачивают как `message`).

```
.log = {
  "database": .database_name,
  "query": .query,
  "application_name": .application_name,
  "message": .pg_message
}
.metrics = [{ "name": "postgresql_log_lines_total", "value": 1, "update": true, "labels": { "database": .database_name } }]
```

```
aggregate {
    vrl file:///var/log/pg.csv name=postgresql_csv
        log_channel_out=pg-json
        start_pattern='^\d{4}-\d{2}-\d{2}'
        condition_pattern='^\s'
        multiline_mode=continue_through;
}
```

Без `.log` / `.logs` ничего не отправляется в `log_channel_out` (только metrics).
См. также `log_channel_raw` для untransformed passthrough.

## Glob file sources (пример PostgreSQL csvlog)

Basename globs работают на `file://` aggregates (см. [aggregate.md](../aggregate.md#glob--match-basename-filter)):

```
vrl {
    name postgresql_csv;
    script /etc/vrl/postgresql_csv.vrl;
}

aggregate {
    vrl file:///spool/postgres-logs/pg/postgresql-2026-08-*.csv name=postgresql_csv
        state=stream
        start_pattern='^\d{4}-\d{2}-\d{2}'
        condition_pattern='^\s'
        multiline_mode=continue_through
        log_level=info;
}
```

Quote patterns с `{` / `}` — иначе plain tokenizer трактует `{` как context brace и обрезает (например, `^\d{4}` → `^\d`).

Эквивалент:

```
aggregate {
    vrl file:///spool/postgres-logs/pg/ match=postgresql-2026-08-*.csv name=postgresql_csv
        state=stream
        start_pattern='^\d{4}-\d{2}-\d{2}'
        condition_pattern='^\s'
        multiline_mode=continue_through;
}
```
