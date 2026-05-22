# chromecdp

Direct Chrome DevTools Protocol (CDP) integration — collects browser loading statistics from Chrome/Chromium headless **without Node.js or the Puppeteer npm package**.

Alligator launches Chrome once as a persistent background process, connects to it over a local WebSocket, and crawls each configured URL in an isolated incognito context on every collection cycle. All metrics are written directly into the alligator metric store.

---

## Requirements

Chrome or Chromium binary with CDP support. No Node.js, no npm required.

| Platform | Package | Binary |
|----------|---------|--------|
| EL7/EL8 (minimal) | `chromium-headless` | `/usr/lib64/chromium-browser/headless_shell` |
| EL7/EL8 (full) | `chromium-browser` | `/usr/bin/chromium-browser` |
| Debian/Ubuntu | `chromium-browser` | `/usr/bin/chromium-browser` |
| Debian/Ubuntu | `google-chrome-stable` | `/usr/bin/google-chrome` |

---

## Minimal config

```
chromecdp {
    executable /usr/bin/chromium-browser;
    https://example.com;
}
```

---

## Module-level options

These go directly inside the `chromecdp { }` block and apply to the whole module.

### `executable`

Path to the Chrome/Chromium binary.
Default: `chromium-browser` (looked up in `PATH`).

```
chromecdp {
    executable /usr/lib64/chromium-browser/headless_shell;
}
```

### `port`

Local TCP port Chrome is started with `--remote-debugging-port`.
Default: `9222`.

```
chromecdp {
    port 9222;
}
```

### `log_level`

Verbosity for chromecdp-specific messages, independent of alligator's global `log_level`. Module messages use `cslog()` (chromecdp `log_level` only); per-URL messages use `carglog()` from the URL block's `log_level`.

| Value | Alias | What is logged |
|-------|-------|---------------|
| `0` | `off` | Hard errors only — Chrome process failures, connect failures (default) |
| `1` | `info` | + Chrome start/stop, WebSocket connected/closed |
| `2` | `debug` | + Page-level warnings (CDP step failures, page errors) |
| `3` | `trace` | + Config parsing details (port, executable, URL registration) |

```
chromecdp {
    log_level info;    # also accepts 0 / 1 / 2 / 3
}
```

### `concurrency`

Maximum number of URLs crawled in parallel within one collection cycle.
Default: `20`.

Higher values shorten full-cycle time when many URLs are configured, at the cost of more Chrome memory and CPU.

```
chromecdp {
    concurrency 25;
}
```

### `batch_size`

How many new URLs to start on each batch tick while a cycle is running.
Default: `2`.

Works together with `batch_interval`: URLs are not all launched at once; they are started in small groups so metrics begin appearing soon after a cycle starts.

```
chromecdp {
    batch_size 5;
}
```

### `batch_interval`

Delay between batch ticks while a cycle is in progress.
Accepts duration strings (`1s`, `500ms`) or integer milliseconds.
Default: `1s` (`1000` ms).

```
chromecdp {
    batch_interval 1s;
}
```

### `setup_budget`

Hard time allowance for CDP setup before navigation (create context, attach, enable domains).
Accepts duration strings or integer milliseconds.
Default: `10s`.

This is **not** the navigation idle wait — see per-URL `timeout` for that.

```
chromecdp {
    setup_budget 15s;
}
```

### `post_nav_budget`

Hard time allowance after navigation for metric collection and teardown (`Performance.getMetrics`, `performance.getEntries()`, optional screenshot, close target, dispose context).
Accepts duration strings or integer milliseconds.
Default: `10s`.

```
chromecdp {
    post_nav_budget 15s;
}
```

Together with per-URL `timeout`, these define the **page hard deadline** used to abort stuck crawls:

```
page_deadline = setup_budget + timeout + post_nav_budget
```

Example: defaults + `timeout 10s` → at most ~30 s per URL from crawl start.

---

## Collection scheduling

Chromecdp uses the global alligator **`aggregate_period`** (same timer as most collectors) as its crawl tick interval. On each tick alligator tries to advance or start a crawl cycle.

A **new** full cycle (all configured URLs) starts only when the previous cycle has finished — no active pages and the URL queue is empty. If one full pass takes longer than `aggregate_period`, timer ticks in between only continue the current cycle (start more batches, check deadlines); they do **not** start a second cycle in parallel.

Typical timeline with `aggregate_period 40s` and ~109 URLs:

| Time | What happens |
|------|----------------|
| 0 s | Cycle starts; first `batch_size` URLs launched |
| every `batch_interval` | More URLs started until queue exhausted (up to `concurrency` parallel) |
| 40 s, 80 s | Timer fires but cycle still running — no new cycle |
| ~120 s | Last pages finish; next tick starts a fresh cycle |

To see a full refresh more often: increase `concurrency`, lower per-URL `timeout`, or raise `batch_size` (if Chrome keeps up). Lowering `aggregate_period` alone does not help while a single cycle still exceeds that interval.

Module options exported via the config API (`GET /config` or equivalent) include `concurrency`, `batch_size`, `batch_interval`, `setup_budget`, and `post_nav_budget` when they differ from defaults.

---

## Per-URL options

These go inside the URL block and apply only to that URL.

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

Maximum time to wait for the page to reach network idle before proceeding.
Accepts duration strings (`5s`, `30s`, `2m`) or integer milliseconds. Default: `10s`.

Network idle is defined as ≤ 2 in-flight requests for at least 2 consecutive seconds (equivalent to Puppeteer's `networkidle2`).

The module-level `setup_budget` and `post_nav_budget` are added to this value to form the per-page hard deadline; if the page exceeds that total, it is aborted with a warning.

### `console_events`

Emit `chromecdp_console_messages_total` for every browser console message during page load.
Set to `true`, `"true"`, or `1` to enable (plain config `console_events true` stores a string). Default: disabled.

Metrics appear only when the page actually calls `console.log` / `console.warn` / etc. during the crawl window. A quiet page produces no series (only HELP/TYPE until the first message).

### `headers`

Extra HTTP request headers sent with every request during page navigation (via `Network.setExtraHTTPHeaders`).

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

Alias for `headers` — also sets extra HTTP headers (for compatibility with `puppeteer` syntax).

### `add_label`

Static key=value labels attached to every metric emitted for this URL.

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

How long emitted metrics survive in the store without being refreshed.
Accepts duration strings (`60s`, `5m`) or integer seconds.
Overrides the global alligator `ttl` for this URL only.

```
chromecdp {
    https://example.com {
        ttl 120s;
    }
}
```

### `log_level`

Per-URL override of the module `log_level`. Useful to enable debug output for one URL without affecting others. Accepts the same values as the module-level option.

### `metricstransform`

Rewrite metric names or label values before they are stored.
Same syntax and semantics as the global `metricstransform` / `action` blocks.

**Plain config example** — strip the path from the `source` label, keeping only the hostname:

```
chromecdp {
    https://example.com {
        metricstransform {
            include ^chromecdp_.*$ match_type regexp label source regex '^https?://([^/]+).*$' replacement '$1';
        }
    }
}
```

**JSON config example** — same transformation:

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

**JSON example — collapse Next.js `/_next/static/` sub-resources** (any host, with optional path prefix before `_next/static`, e.g. `https://www.example.com/_next/static/...` or `https://cdn.example.com/app/v1/_next/static/...`):

Use a pattern that matches the **entire** `source` URL and keeps only the prefix through `_next/static/`. A capture that ends at `_next/static/` but leaves `css/...` or `chunks/...` outside the match does nothing (the suffix stays).

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

| Example `source` before | After |
|-------------------------|-------|
| `https://www.example.com/_next/static/css/app.css` | `https://www.example.com/_next/static/` |
| `https://cdn.example.com/app/v1/_next/static/chunks/pages/home-abc123.js` | `https://cdn.example.com/app/v1/_next/static/` |

`^(.+/_next/static/).*` is host-independent: any scheme/host/path prefix is allowed as long as `/_next/static/` appears in the URL. If you only need `https` URLs with nothing between host and `_next/static/`, `^(https://[^/]+/_next/static/).*` is enough for that layout only.

**Debugging `metricstransform`** — set per-URL or module `log_level` to `trace`. Alligator logs each regex step at trace priority, for example:

- `metricstransform: metric 'chromecdp_resource_size_bytes' apply`
- `metricstransform: ... step 0 OK regex '...' repl '...': 'before' -> 'after'`
- `metricstransform: ... step 0 NO MATCH` / `COMPILE ERROR` / `UNCHANGED`

Set `log_level: trace` on the URL or `chromecdp` module. Trace lines use the URL `carg` when metrics are emitted; module lines use `cslog` and only require `chromecdp.log_level trace` (global `log_level` does not gate `cslog`).

### `screenshot`

Capture a page screenshot and save it to disk when the HTTP response code meets the threshold.

| Key | Type | Description |
|-----|------|-------------|
| `minimum_code` | integer | Minimum response status to trigger (e.g. `400` saves on 4xx/5xx) |
| `type` | string | Image format, currently `png` |
| `dir` | string | Directory to write files to. Default: `/var/lib/alligator/` |
| `fullPage` | bool | Capture full scrollable page if `true`. Default: `false` |

Filename pattern: `<sanitised-url>-<ISO-timestamp>.png`

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

*(Stub — registers a request interception pattern. Full POST-body injection via the `Fetch` CDP domain is not yet implemented.)*

---

## How a crawl works

Each collection cycle for a URL follows these sequential CDP steps:

| Step | CDP call | Description |
|------|----------|-------------|
| 1 | `Target.createBrowserContext` | Fresh incognito browser context |
| 2 | `Target.createTarget` | New blank page inside that context |
| 3 | `Target.attachToTarget` | Attach with a flat CDP session |
| 4 | `Network.enable` + `Runtime.enable` + `Performance.enable` | Enable event domains |
| 4a | `Network.setCacheDisabled` | Disable browser cache for reproducible results |
| 4b | `Emulation.setDeviceMetricsOverride` | Set viewport 640×360 |
| 4c | `Network.setExtraHTTPHeaders` | Apply `headers` / `env` config (if set) |
| 5 | `Page.navigate` | Navigate to the target URL |
| 6 | *(idle timer)* | Wait for networkidle2 (≤2 in-flight for ≥2 s) or `timeout` |
| 7 | `Performance.getMetrics` | Collect Chrome renderer performance counters |
| 8 | `Runtime.evaluate` | Evaluate `JSON.stringify(performance.getEntries())` |
| 9 | `Page.captureScreenshot` | *(only if `screenshot` is configured and threshold met)* |
| 10 | `Target.closeTarget` | Close the page |
| 11 | `Target.disposeBrowserContext` | Destroy the incognito context |

Chrome is launched once with `--headless --no-sandbox --remote-debugging-port=<port>` and reused across all collection cycles. A 2.5-second startup delay is applied after `uv_spawn` before the first connection attempt.

---

## Emitted metrics

All metrics carry at minimum a `resource` label with the target URL.

### Lifecycle

| Metric | Type | Description |
|--------|------|-------------|
| `chromecdp_info` | gauge | Set to `1` when a crawl starts for this resource |
| `chromecdp_navigation_errors_total` | counter | Incremented only when `Page.navigate` returns a **CDP error** (protocol failure). HTTP 4xx/5xx still count as successful navigation and do **not** increment this counter. Absent from `/metrics` until at least one such error occurs. |

### Chrome `Performance.getMetrics`

Collected via `Performance.getMetrics` after network idle. Labels: `resource`.

#### Timestamps (Chrome monotonic clock, seconds)

| Metric | Chrome field | Description |
|--------|-------------|-------------|
| `chromecdp_timestamp_seconds` | `Timestamp` | Collection timestamp |
| `chromecdp_navigation_start_seconds` | `NavigationStart` | Navigation start event |
| `chromecdp_first_meaningful_paint_seconds` | `FirstMeaningfulPaint` | First Meaningful Paint |
| `chromecdp_dom_content_loaded_seconds` | `DomContentLoaded` | DOMContentLoaded event |

#### DOM / JavaScript gauges

| Metric | Chrome field | Description |
|--------|-------------|-------------|
| `chromecdp_documents` | `Documents` | Document objects in the frame tree |
| `chromecdp_frames` | `Frames` | Frame elements |
| `chromecdp_nodes` | `Nodes` | Total DOM nodes |
| `chromecdp_js_event_listeners` | `JSEventListeners` | Registered JS event listeners |
| `chromecdp_js_heap_used_bytes` | `JSHeapUsedSize` | Used JS heap in bytes |
| `chromecdp_js_heap_total_bytes` | `JSHeapTotalSize` | Total allocated JS heap in bytes |
| `chromecdp_array_buffer_bytes` | `ArrayBufferContents` | Bytes held by ArrayBuffer objects |
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

| Metric | Chrome field | Type | Description |
|--------|-------------|------|-------------|
| `chromecdp_layouts_total` | `LayoutCount` | counter | Full or partial layout operations |
| `chromecdp_style_recalcs_total` | `RecalcStyleCount` | counter | Style recalculation operations |
| `chromecdp_layout_duration_seconds_total` | `LayoutDuration` | counter | Combined layout time in seconds |
| `chromecdp_style_recalc_duration_seconds_total` | `RecalcStyleDuration` | counter | Combined style recalc time in seconds |
| `chromecdp_script_duration_seconds_total` | `ScriptDuration` | counter | JavaScript execution time in seconds |
| `chromecdp_v8_compile_duration_seconds_total` | `V8CompileDuration` | counter | V8 compilation time in seconds |
| `chromecdp_task_duration_seconds_total` | `TaskDuration` | counter | All renderer task time in seconds |
| `chromecdp_task_other_duration_seconds_total` | `TaskOtherDuration` | counter | Unattributed renderer task time |
| `chromecdp_thread_time_seconds_total` | `ThreadTime` | counter | Renderer thread CPU time |
| `chromecdp_process_time_seconds_total` | `ProcessTime` | counter | Renderer process CPU time |
| `chromecdp_devtools_command_duration_seconds_total` | `DevToolsCommandDuration` | counter | Time processing DevTools commands |

Unknown future `Performance.getMetrics` entries not in the table above are emitted as `chromecdp_<ChromeName>`.

### Network domain — per-resource

Emitted from CDP `Network.*` events during page load. Labels: `resource` (target URL), `source` (sub-resource URL, truncated to 128 chars).

| Metric | Type | Description |
|--------|------|-------------|
| `chromecdp_resource_http_status` | gauge | HTTP response status code per sub-resource |
| `chromecdp_resource_duration_milliseconds` | gauge | Time from request start to load complete (ms) |
| `chromecdp_resource_size_bytes` | gauge | Encoded (compressed) bytes transferred |
| `chromecdp_resource_failures_total` | counter | Sub-requests that failed to load |

### Resource Timing API

Emitted at crawl **step 8** (`Runtime.evaluate` → `performance.getEntries()`), after network idle and `Performance.getMetrics`. If the page hits the hard deadline earlier, these metrics are not produced.

Entries are exported when **`transferSize > 0`** (same rule as the `puppeteer` collector for sub-resources) **or** `entryType` is **`navigation`** (main document timings are always kept, even when `transferSize` is 0). Cross-origin sub-resources with `transferSize == 0` are skipped.

The collector evaluates a bounded payload: all `navigation` entries plus up to 250 `resource` entries with `transferSize > 0`. A full `performance.getEntries()` JSON blob can exceed Chrome CDP `returnByValue` limits on heavy pages (for example `eda.ru`), in which case step 8 produced no metrics before this cap.

On failure or empty export, alligator logs `chromecdp: getEntries ...` at **warn** (check alligator stderr; per-URL `log_level: debug` is optional).

Labels: `resource`, `source`, `entryType`, `initiatorType`, `nextHopProtocol`.

| Metric | JS field | Description |
|--------|----------|-------------|
| `chromecdp_rt_start_milliseconds` | `startTime` | Entry start time relative to navigationStart |
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
| `chromecdp_rt_transfer_bytes` | `transferSize` | Bytes transferred over the network |
| `chromecdp_rt_encoded_body_bytes` | `encodedBodySize` | Compressed response body size |
| `chromecdp_rt_decoded_body_bytes` | `decodedBodySize` | Uncompressed response body size |

### Aggregate

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `chromecdp_page_size_bytes` | gauge | `resource` | Total transfer size of all same-origin resources |

### Console / JS errors (optional)

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `chromecdp_console_messages_total` | counter | `resource`, `text` | Browser `console.*` calls during load — **requires** per-URL `console_events: true` (see below). Absent until a message is logged. `text` is truncated to 128 chars and escaped for Prometheus (`\`, `"`, `'`, tabs → spaces). |
| `chromecdp_page_errors_total` | counter | `resource`, `text` | **Uncaught** JavaScript exceptions (`Runtime.exceptionThrown`). `console.error` does not count. Absent until an exception occurs. Same `text` normalization as console messages. |

---

## Full config example

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

## Full config example (JSON)

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

## Comparison with `puppeteer`

| Feature | `puppeteer` | `chromecdp` |
|---------|-------------|-------------|
| Runtime dependency | Node.js + puppeteer npm | None |
| Chrome management | Puppeteer launches Chrome per cycle | Alligator keeps Chrome persistent |
| Process overhead | Node.js + Chrome on every cycle | Chrome started once |
| CDP transport | Puppeteer internal pipe | Native WebSocket in alligator |
| Metrics ingestion | Prometheus text via stdout → parse | Direct `metric_add()` |
| Config block | `puppeteer { }` | `chromecdp { }` |
| Metric prefix | `puppeteer_` | `chromecdp_` |
| Per-URL options | `timeout`, `console_events`, `add_label`, `headers`, `screenshot`, `metricstransform` | Same + `ttl`, `log_level` |
| Parallel crawl tuning | N/A (Node puppeteer script) | `concurrency`, `batch_size`, `batch_interval`, `setup_budget`, `post_nav_budget` |
| Collection period | Driven by script / external cron | Global `aggregate_period`; one cycle at a time |

Both contexts can coexist — the `puppeteer` block remains fully functional alongside `chromecdp`.
