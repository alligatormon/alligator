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

Verbosity for chromecdp-specific messages, independent of alligator's global `log_level`.

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
Accepts duration strings (`5s`, `30s`, `2m`). Default: `10s`.

Network idle is defined as ≤ 2 in-flight requests for at least 2 consecutive seconds (equivalent to Puppeteer's `networkidle2`).

### `console_events`

Emit `chromecdp_console_messages_total` for every browser console message during page load.
Set to `true` or `1` to enable. Default: disabled.

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
| `chromecdp_navigation_errors_total` | counter | CDP navigation errors; `error` label contains the message |

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

Emitted from `window.performance.getEntries()` evaluated after network idle. Labels: `resource`, `source`, `entryType`, `initiatorType`, `nextHopProtocol`.

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
| `chromecdp_console_messages_total` | counter | `resource`, `text` | Browser console messages (requires `console_events: true`) |
| `chromecdp_page_errors_total` | counter | `resource`, `text` | Uncaught JavaScript exceptions |

---

## Full config example

```
chromecdp {
    executable /usr/bin/chromium-browser;
    port       9222;
    log_level  info;

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

Both contexts can coexist — the `puppeteer` block remains fully functional alongside `chromecdp`.
