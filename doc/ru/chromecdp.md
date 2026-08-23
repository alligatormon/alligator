**Language / Язык:** [English](../chromecdp.md) | [Русский](chromecdp.md)

# chromecdp

Прямая интеграция с Chrome DevTools Protocol (CDP) — собирает статистику загрузки браузера из headless Chrome/Chromium **без Node.js и npm-пакета Puppeteer**.

Alligator один раз запускает Chrome как постоянный фоновый процесс, подключается к нему по локальному WebSocket и на каждом цикле сбора обходит каждый настроенный URL в изолированном incognito-контексте. Все метрики записываются напрямую в metric store alligator.

---

## Требования

Бинарник Chrome или Chromium с поддержкой CDP. Node.js и npm не нужны.

| Платформа | Пакет | Бинарник |
|----------|---------|--------|
| EL7/EL8 (minimal) | `chromium-headless` | `/usr/lib64/chromium-browser/headless_shell` |
| EL7/EL8 (full) | `chromium-browser` | `/usr/bin/chromium-browser` |
| Debian/Ubuntu | `chromium-browser` | `/usr/bin/chromium-browser` |
| Debian/Ubuntu | `google-chrome-stable` | `/usr/bin/google-chrome` |

---

## Минимальная конфигурация

```
chromecdp {
    executable /usr/bin/chromium-browser;
    https://example.com;
}
```

---

## Опции уровня модуля

Задаются непосредственно внутри блока `chromecdp { }` и применяются ко всему модулю.

### `executable`

Путь к бинарнику Chrome/Chromium.
По умолчанию: `chromium-browser` (ищется в `PATH`).

```
chromecdp {
    executable /usr/lib64/chromium-browser/headless_shell;
}
```

### `port`

Локальный TCP-порт, с которым Chrome запускается через `--remote-debugging-port`.
По умолчанию: `9222`.

```
chromecdp {
    port 9222;
}
```

### `log_level`

Детализация сообщений chromecdp, независимо от глобального `log_level` alligator. Сообщения модуля используют `cslog()` (только `log_level` chromecdp); сообщения per-URL — `carglog()` из `log_level` блока URL.

| Значение | Alias | Что логируется |
|-------|-------|---------------|
| `0` | `off` | Только жёсткие ошибки — сбои процесса Chrome, ошибки подключения (по умолчанию) |
| `1` | `info` | + старт/стоп Chrome, WebSocket connected/closed |
| `2` | `debug` | + предупреждения уровня страницы (сбои CDP step, page errors) |
| `3` | `trace` | + детали разбора конфигурации (port, executable, регистрация URL) |

```
chromecdp {
    log_level info;    # также принимает 0 / 1 / 2 / 3
}
```

### `concurrency`

Максимальное число URL, обходимых параллельно в одном цикле сбора.
По умолчанию: `20`.

Более высокие значения сокращают время полного цикла при большом числе URL, ценой большего потребления памяти и CPU Chrome.

```
chromecdp {
    concurrency 25;
}
```

### `batch_size`

Сколько новых URL запускать на каждом batch tick, пока цикл выполняется.
По умолчанию: `2`.

Работает вместе с `batch_interval`: URL не запускаются все сразу; они стартуют небольшими группами, чтобы метрики появлялись вскоре после начала цикла.

```
chromecdp {
    batch_size 5;
}
```

### `batch_interval`

Задержка между batch tick, пока цикл в процессе.
Принимает строки длительности (`1s`, `500ms`) или целые миллисекунды.
По умолчанию: `1s` (`1000` ms).

```
chromecdp {
    batch_interval 1s;
}
```

### `setup_budget`

Жёсткий лимит времени на CDP setup до навигации (create context, attach, enable domains).
Принимает строки длительности или целые миллисекунды.
По умолчанию: `10s`.

Это **не** ожидание network idle при навигации — см. per-URL `timeout`.

```
chromecdp {
    setup_budget 15s;
}
```

### `post_nav_budget`

Жёсткий лимит времени после навигации на сбор метрик и teardown (`Performance.getMetrics`, `performance.getEntries()`, опциональный screenshot, close target, dispose context).
Принимает строки длительности или целые миллисекунды.
По умолчанию: `10s`.

```
chromecdp {
    post_nav_budget 15s;
}
```

Вместе с per-URL `timeout` они задают **page hard deadline** для прерывания зависших crawl:

```
page_deadline = setup_budget + timeout + post_nav_budget
```

Пример: defaults + `timeout 10s` → максимум ~30 с на URL с начала crawl.

---

## Планирование сбора

Chromecdp использует глобальный alligator **`aggregate_period`** (тот же таймер, что у большинства collectors) как интервал crawl tick. На каждом tick alligator пытается продвинуть или начать crawl cycle.

**Новый** полный цикл (все настроенные URL) начинается только когда предыдущий завершён — нет активных страниц и очередь URL пуста. Если один полный проход длиннее `aggregate_period`, промежуточные timer tick только продолжают текущий цикл (запускают batch, проверяют deadlines); они **не** стартуют второй цикл параллельно.

Типичная timeline с `aggregate_period 40s` и ~109 URL:

| Время | Что происходит |
|------|----------------|
| 0 s | Старт цикла; запуск первых `batch_size` URL |
| каждые `batch_interval` | Запуск следующих URL, пока очередь не исчерпана (до `concurrency` параллельно) |
| 40 s, 80 s | Timer срабатывает, но цикл ещё идёт — нового цикла нет |
| ~120 s | Последние страницы завершены; следующий tick начинает новый цикл |

Чтобы чаще видеть полное обновление: увеличьте `concurrency`, уменьшите per-URL `timeout` или поднимите `batch_size` (если Chrome успевает). Одно лишь уменьшение `aggregate_period` не поможет, пока один цикл всё ещё длиннее этого интервала.

Опции модуля, экспортируемые через config API (`GET /config` или эквивалент), включают `concurrency`, `batch_size`, `batch_interval`, `setup_budget` и `post_nav_budget`, если они отличаются от defaults.

---

## Опции per-URL

Задаются внутри блока URL и применяются только к этому URL.

```
chromecdp {
    https://example.com {
        timeout        10s;
        console_events true;
        add_label { team sre; }
        ttl            120s;
    }
}
```

### `timeout`

Максимальное время ожидания network idle страницы перед продолжением.
Принимает строки длительности (`5s`, `30s`, `2m`) или целые миллисекунды. По умолчанию: `10s`.

Network idle — ≤ 2 in-flight запросов не менее 2 секунд подряд (эквивалент Puppeteer `networkidle2`).

Module-level `setup_budget` и `post_nav_budget` добавляются к этому значению для per-page hard deadline; при превышении страница прерывается с warning.

### `console_events`

Emit `chromecdp_console_messages_total` для каждого browser console message во время загрузки страницы.
Включите `true`, `"true"` или `1` (plain config `console_events true` хранит string). По умолчанию: выключено.

Метрики появляются только если страница реально вызывает `console.log` / `console.warn` / и т. д. в окне crawl. Тихая страница не даёт series (только HELP/TYPE до первого сообщения).

### `headers`

Дополнительные HTTP request headers для каждого запроса при навигации (через `Network.setExtraHTTPHeaders`).

```
chromecdp {
    https://example.com {
        headers {
            Authorization "Bearer token123";
            X-Custom-Header value;
        }
    }
}
```

### `env`

Alias для `headers` — тоже задаёт extra HTTP headers (совместимость с синтаксисом `puppeteer`).

### `add_label`

Статические key=value labels для каждой метрики, emit-нутой для этого URL.

```
chromecdp {
    https://example.com {
        add_label {
            team    sre;
            service web-check;
        }
    }
}
```

### `ttl`

Сколько emit-нутые метрики живут в store без refresh.
Принимает строки длительности (`60s`, `5m`) или целые секунды.
Переопределяет глобальный alligator `ttl` только для этого URL.

```
chromecdp {
    https://example.com {
        ttl 120s;
    }
}
```

### `log_level`

Per-URL override module `log_level`. Полезно включить debug для одного URL без влияния на остальные. Принимает те же значения, что module-level option.

### `metricstransform`

Переписывает имена метрик или значения labels перед записью в store.
Тот же синтаксис и семантика, что у глобальных блоков `metricstransform` / `action`.

**Пример plain config** — убрать path из label `source`, оставить только hostname:

```
chromecdp {
    https://example.com {
        metricstransform {
            include ^chromecdp_.*$ match_type regexp label source regex '^https?://([^/]+).*$' replacement '$1';
        }
    }
}
```

**Пример JSON config** — та же трансформация:

```json
"metricstransform": {
    "transforms": [
        {
            "include": "chromecdp_resource_http_status",
            "match_type": "strict",
            "operations": [
                {
                    "action": "update_label",
                    "label": "source",
                    "value_actions": [
                        {
                            "regex": "^https?://([^/]+).*$",
                            "replacement": "$1"
                        }
                    ]
                }
            ]
        }
    ]
}
```

**JSON example — collapse Next.js `/_next/static/` sub-resources** (любой host, с опциональным path prefix перед `_next/static`, например `https://www.example.com/_next/static/...` или `https://cdn.example.com/app/v1/_next/static/...`):

Используйте паттерн, matching **весь** URL `source` и оставляющий только prefix до `_next/static/`. Capture, заканчивающийся на `_next/static/`, но оставляющий `css/...` или `chunks/...` вне match, ничего не делает (suffix остаётся).

```json
"metricstransform": {
    "transforms": [
        {
            "include": ".*",
            "match_type": "regexp",
            "operations": [
                {
                    "action": "update_label",
                    "label": "source",
                    "value_actions": [
                        {
                            "regex": "^(.+/_next/static/).*",
                            "replacement": "$1"
                        },
                        {
                            "regex": "^(data:[^;]+).*$",
                            "replacement": "data:uri"
                        }
                    ]
                }
            ]
        }
    ]
}
```

| Пример `source` до | После |
|-------------------------|-------|
| `https://www.example.com/_next/static/css/app.css` | `https://www.example.com/_next/static/` |
| `https://cdn.example.com/app/v1/_next/static/chunks/pages/home-abc123.js` | `https://cdn.example.com/app/v1/_next/static/` |

`^(.+/_next/static/).*` host-independent: любой scheme/host/path prefix, если в URL есть `/_next/static/`. Если нужны только `https` URL без ничего между host и `_next/static/`, достаточно `^(https://[^/]+/_next/static/).*` только для такой layout.

**Отладка `metricstransform`** — задайте per-URL или module `log_level` в `trace`. Alligator логирует каждый regex step с trace priority, например:

- `metricstransform: metric 'chromecdp_resource_size_bytes' apply`
- `metricstransform: ... step 0 OK regex '...' repl '...': 'before' -> 'after'`
- `metricstransform: ... step 0 NO MATCH` / `COMPILE ERROR` / `UNCHANGED`

Задайте `log_level: trace` на URL или module `chromecdp`. Trace lines используют URL `carg` при emit metrics; module lines — `cslog` и требуют только `chromecdp.log_level trace` (global `log_level` не gate-ит `cslog`).

### `screenshot`

Capture screenshot страницы и сохранение на диск, когда HTTP response code достигает threshold.

| Ключ | Тип | Описание |
|-----|------|-------------|
| `minimum_code` | integer | Минимальный status для trigger (например, `400` — save на 4xx/5xx) |
| `type` | string | Формат изображения, сейчас `png` |
| `dir` | string | Каталог для файлов. По умолчанию: `/var/lib/alligator/` |
| `fullPage` | bool | Полная прокручиваемая страница при `true`. По умолчанию: `false` |

Шаблон имени файла: `<sanitised-url>-<ISO-timestamp>.png`

```
chromecdp {
    https://example.com {
        screenshot {
            minimum_code 400;
            type         png;
            dir          /var/lib/alligator/screenshots/;
            fullPage     false;
        }
    }
}
```

### `post_data`

*(Stub — регистрирует request interception pattern. Полная POST-body injection через CDP domain `Fetch` ещё не реализована.)*

---

## Как работает crawl

Каждый collection cycle для URL следует этим последовательным CDP steps:

| Шаг | CDP call | Описание |
|------|----------|-------------|
| 1 | `Target.createBrowserContext` | Свежий incognito browser context |
| 2 | `Target.createTarget` | Новая blank page в этом context |
| 3 | `Target.attachToTarget` | Attach с flat CDP session |
| 4 | `Network.enable` + `Runtime.enable` + `Performance.enable` | Enable event domains |
| 4a | `Network.setCacheDisabled` | Отключить browser cache для воспроизводимости |
| 4b | `Emulation.setDeviceMetricsOverride` | Viewport 640×360 |
| 4c | `Network.setExtraHTTPHeaders` | Применить `headers` / `env` config (если задано) |
| 5 | `Page.navigate` | Navigate к target URL |
| 6 | *(idle timer)* | Ждать networkidle2 (≤2 in-flight ≥2 s) или `timeout` |
| 7 | `Performance.getMetrics` | Собрать Chrome renderer performance counters |
| 8 | `Runtime.evaluate` | Evaluate `JSON.stringify(performance.getEntries())` |
| 9 | `Page.captureScreenshot` | *(только если `screenshot` настроен и threshold met)* |
| 10 | `Target.closeTarget` | Закрыть page |
| 11 | `Target.disposeBrowserContext` | Уничтожить incognito context |

Chrome запускается один раз с `--headless --no-sandbox --remote-debugging-port=<port>` и переиспользуется между collection cycles. После `uv_spawn` применяется 2.5-секундная startup delay перед первой попыткой подключения.

---

## Emit-нутые метрики

Все метрики несут как минимум label `resource` с target URL.

### Lifecycle

| Metric | Type | Описание |
|--------|------|-------------|
| `chromecdp_info` | gauge | `1` при старте crawl для этого resource |
| `chromecdp_navigation_errors_total` | counter | Increment только когда `Page.navigate` возвращает **CDP error** (protocol failure). HTTP 4xx/5xx всё ещё successful navigation и **не** increment-ят counter. Отсутствует в `/metrics`, пока не было такой ошибки. |

### Chrome `Performance.getMetrics`

Собирается через `Performance.getMetrics` после network idle. Labels: `resource`.

#### Timestamps (Chrome monotonic clock, seconds)

| Metric | Chrome field | Описание |
|--------|-------------|-------------|
| `chromecdp_timestamp_seconds` | `Timestamp` | Timestamp сбора |
| `chromecdp_navigation_start_seconds` | `NavigationStart` | Событие navigation start |
| `chromecdp_first_meaningful_paint_seconds` | `FirstMeaningfulPaint` | First Meaningful Paint |
| `chromecdp_dom_content_loaded_seconds` | `DomContentLoaded` | Событие DOMContentLoaded |

#### DOM / JavaScript gauges

| Metric | Chrome field | Описание |
|--------|-------------|-------------|
| `chromecdp_documents` | `Documents` | Document objects в frame tree |
| `chromecdp_frames` | `Frames` | Frame elements |
| `chromecdp_nodes` | `Nodes` | Всего DOM nodes |
| `chromecdp_js_event_listeners` | `JSEventListeners` | Зарегистрированные JS event listeners |
| `chromecdp_js_heap_used_bytes` | `JSHeapUsedSize` | Used JS heap в bytes |
| `chromecdp_js_heap_total_bytes` | `JSHeapTotalSize` | Total allocated JS heap в bytes |
| `chromecdp_array_buffer_bytes` | `ArrayBufferContents` | Bytes в ArrayBuffer objects |
| `chromecdp_audio_handlers` | `AudioHandlers` | Live Web Audio API nodes |
| `chromecdp_audio_worklet_processors` | `AudioWorkletProcessors` | Active AudioWorkletProcessor instances |
| `chromecdp_ad_subframes` | `AdSubframes` | Ad-tagged subframes |
| `chromecdp_rtc_peer_connections` | `RTCPeerConnections` | Active RTCPeerConnection objects |
| `chromecdp_worker_global_scopes` | `WorkerGlobalScopes` | Active WorkerGlobalScope instances |
| `chromecdp_resources` | `Resources` | Cached Resource objects |
| `chromecdp_resource_fetchers` | `ResourceFetchers` | ResourceFetcher instances |
| `chromecdp_ua_css_resources` | `UACSSResources` | UA CSS resources |
| `chromecdp_v8_per_context_datas` | `V8PerContextDatas` | V8 per-context data objects |
| `chromecdp_context_lifecycle_observers` | `ContextLifecycleStateObservers` | Context lifecycle observers |
| `chromecdp_detached_script_states` | `DetachedScriptStates` | Detached script execution contexts |
| `chromecdp_media_key_sessions` | `MediaKeySessions` | Active MediaKeySession objects |
| `chromecdp_media_keys` | `MediaKeys` | Active MediaKeys objects |

#### Layout / script counters

| Metric | Chrome field | Type | Описание |
|--------|-------------|------|-------------|
| `chromecdp_layouts_total` | `LayoutCount` | counter | Full или partial layout operations |
| `chromecdp_style_recalcs_total` | `RecalcStyleCount` | counter | Style recalculation operations |
| `chromecdp_layout_duration_seconds_total` | `LayoutDuration` | counter | Combined layout time в seconds |
| `chromecdp_style_recalc_duration_seconds_total` | `RecalcStyleDuration` | counter | Combined style recalc time в seconds |
| `chromecdp_script_duration_seconds_total` | `ScriptDuration` | counter | JavaScript execution time в seconds |
| `chromecdp_v8_compile_duration_seconds_total` | `V8CompileDuration` | counter | V8 compilation time в seconds |
| `chromecdp_task_duration_seconds_total` | `TaskDuration` | counter | All renderer task time в seconds |
| `chromecdp_task_other_duration_seconds_total` | `TaskOtherDuration` | counter | Unattributed renderer task time |
| `chromecdp_thread_time_seconds_total` | `ThreadTime` | counter | Renderer thread CPU time |
| `chromecdp_process_time_seconds_total` | `ProcessTime` | counter | Renderer process CPU time |
| `chromecdp_devtools_command_duration_seconds_total` | `DevToolsCommandDuration` | counter | Time processing DevTools commands |

Unknown future entries `Performance.getMetrics`, не в таблице выше, emit-ятся как `chromecdp_<ChromeName>`.

### Network domain — per-resource

Emit из CDP `Network.*` events во время page load. Labels: `resource` (target URL), `source` (sub-resource URL, truncated до 128 chars).

| Metric | Type | Описание |
|--------|------|-------------|
| `chromecdp_resource_http_status` | gauge | HTTP response status code per sub-resource |
| `chromecdp_resource_duration_milliseconds` | gauge | Time от request start до load complete (ms) |
| `chromecdp_resource_size_bytes` | gauge | Encoded (compressed) bytes transferred |
| `chromecdp_resource_failures_total` | counter | Sub-requests, не загрузившиеся |

### Resource Timing API

Emit на crawl **step 8** (`Runtime.evaluate` → `performance.getEntries()`), после network idle и `Performance.getMetrics`. Если страница раньше hit hard deadline, эти metrics не produce-ятся.

Entries export-ятся когда **`transferSize > 0`** (то же правило, что у collector `puppeteer` для sub-resources) **или** `entryType` — **`navigation`** (main document timings всегда keep, даже при `transferSize` 0). Cross-origin sub-resources с `transferSize == 0` skip.

Collector evaluate bounded payload: все `navigation` entries плюс до 250 `resource` entries с `transferSize > 0`. Полный JSON blob `performance.getEntries()` может превысить Chrome CDP `returnByValue` limits на heavy pages (например, `eda.ru`), тогда step 8 не давал metrics до этого cap.

При failure или empty export alligator логирует `chromecdp: getEntries ...` на **warn** (смотрите alligator stderr; per-URL `log_level: debug` опционален).

Labels: `resource`, `source`, `entryType`, `initiatorType`, `nextHopProtocol`.

| Metric | JS field | Описание |
|--------|----------|-------------|
| `chromecdp_rt_start_milliseconds` | `startTime` | Entry start time относительно navigationStart |
| `chromecdp_rt_duration_milliseconds` | `duration` | Total entry duration |
| `chromecdp_rt_worker_start_milliseconds` | `workerStart` | Service Worker start time |
| `chromecdp_rt_redirect_start_milliseconds` | `redirectStart` | First redirect start |
| `chromecdp_rt_redirect_end_milliseconds` | `redirectEnd` | Last redirect end |
| `chromecdp_rt_fetch_start_milliseconds` | `fetchStart` | Fetch start time |
| `chromecdp_rt_dns_start_milliseconds` | `domainLookupStart` | DNS lookup start |
| `chromecdp_rt_dns_end_milliseconds` | `domainLookupEnd` | DNS lookup end |
| `chromecdp_rt_ssl_start_milliseconds` | `secureConnectionStart` | TLS handshake start |
| `chromecdp_rt_connect_start_milliseconds` | `connectStart` | TCP connection start |
| `chromecdp_rt_connect_end_milliseconds` | `connectEnd` | TCP connection end |
| `chromecdp_rt_request_start_milliseconds` | `requestStart` | Request sent time |
| `chromecdp_rt_response_start_milliseconds` | `responseStart` | Time to first byte (TTFB) |
| `chromecdp_rt_response_end_milliseconds` | `responseEnd` | Response fully received |
| `chromecdp_rt_transfer_bytes` | `transferSize` | Bytes transferred over network |
| `chromecdp_rt_encoded_body_bytes` | `encodedBodySize` | Compressed response body size |
| `chromecdp_rt_decoded_body_bytes` | `decodedBodySize` | Uncompressed response body size |

### Aggregate

| Metric | Type | Labels | Описание |
|--------|------|--------|-------------|
| `chromecdp_page_size_bytes` | gauge | `resource` | Total transfer size всех same-origin resources |

### Console / JS errors (optional)

| Metric | Type | Labels | Описание |
|--------|------|--------|-------------|
| `chromecdp_console_messages_total` | counter | `resource`, `text` | Browser `console.*` calls during load — **требует** per-URL `console_events: true` (см. ниже). Absent до logged message. `text` truncated до 128 chars и escaped для Prometheus (`\`, `"`, `'`, tabs → spaces). |
| `chromecdp_page_errors_total` | counter | `resource`, `text` | **Uncaught** JavaScript exceptions (`Runtime.exceptionThrown`). `console.error` не считается. Absent до exception. Та же `text` normalization, что у console messages. |

---

## Полный пример конфигурации

```
chromecdp {
    executable      /usr/bin/chromium-browser;
    port            9222;
    log_level       info;
    concurrency     25;
    batch_size      2;
    batch_interval  1s;
    setup_budget    10s;
    post_nav_budget 10s;

    https://example.com {
        timeout        10s;
        ttl            120s;
        console_events true;
        log_level      debug;

        add_label {
            team    sre;
            service web-check;
        }

        headers {
            Authorization "Bearer token123";
        }

        screenshot {
            minimum_code 400;
            type         png;
            dir          /var/lib/alligator/screenshots/;
        }
    }

    https://api.example.com {
        timeout   5s;
        add_label { team sre; service api; }
    }
}
```

## Полный пример конфигурации (JSON)

```json
"chromecdp": {
    "executable": "/usr/bin/chromium-browser",
    "port": 9222,
    "log_level": "info",
    "concurrency": 25,
    "batch_size": 2,
    "batch_interval": "1s",
    "setup_budget": "10s",
    "post_nav_budget": "10s",

    "https://example.com": {
        "timeout": "10s",
        "ttl": "120s",
        "console_events": true,
        "log_level": "debug",

        "add_label": {
            "team": "sre",
            "service": "web-check"
        },

        "headers": {
            "Authorization": "Bearer token123"
        },

        "screenshot": {
            "minimum_code": 400,
            "type": "png",
            "dir": "/var/lib/alligator/screenshots/",
            "fullPage": false
        },

        "metricstransform": {
            "transforms": [
                {
                    "include": "chromecdp_resource_http_status",
                    "match_type": "strict",
                    "operations": [
                        {
                            "action": "update_label",
                            "label": "source",
                            "value_actions": [
                                {
                                    "regex": "^https?://([^/]+).*$",
                                    "replacement": "$1"
                                }
                            ]
                        }
                    ]
                }
            ]
        }
    }
}
```

---

## Сравнение с `puppeteer`

| Feature | `puppeteer` | `chromecdp` |
|---------|-------------|-------------|
| Runtime dependency | Node.js + puppeteer npm | None |
| Chrome management | Puppeteer launches Chrome per cycle | Alligator keeps Chrome persistent |
| Process overhead | Node.js + Chrome on every cycle | Chrome started once |
| CDP transport | Puppeteer internal pipe | Native WebSocket in alligator |
| Metrics ingestion | Prometheus text via stdout → parse | Direct `metric_add()` |
| Config block | `puppeteer { }` | `chromecdp { }` |
| Metric prefix | `puppeteer_` | `chromecdp_` |
| Per-URL options | `timeout`, `console_events`, `add_label`, `headers`, `screenshot`, `metricstransform` | То же + `ttl`, `log_level` |
| Parallel crawl tuning | N/A (Node puppeteer script) | `concurrency`, `batch_size`, `batch_interval`, `setup_budget`, `post_nav_budget` |
| Collection period | Driven by script / external cron | Global `aggregate_period`; one cycle at a time |

Оба context могут coexist — блок `puppeteer` остаётся полностью functional рядом с `chromecdp`.
