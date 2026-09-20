# Probe modules

**Language / Язык:** [English](../probe.md) | [Русский](probe.md)

Контекст `probe` задаёт переиспользуемые модули blackbox-проверок. Они вызываются по запросу через `GET /probe` на любом HTTP entrypoint, аналогично Prometheus Blackbox Exporter.

Периодические проверки по-прежнему настраиваются через `aggregate { blackbox … }`. Контекст `probe` нужен, когда цель выбирается при запросе (Prometheus, Alertmanager, скрипты).

## Обзор

```
entrypoint {
    handler prometheus;
    tcp 1111;
}

probe {
    name http_2xx;
    prober http;
    follow_redirects 5;
    valid_status_codes 2xx;
    add_label probe:http;
}

probe {
    name icmp;
    prober icmp;
    timeout 5s;
    loop 10;
    percent 0.5;
}
```

Запрос:

```
curl 'http://127.0.0.1:1111/probe?module=http_2xx&target=example.com'
```

Встроенных модулей **нет**. Имя `http_2xx` из примера работает только после `probe { name http_2xx; … }`. Неизвестный `module` возвращает HTTP 400 (`no such module '…'`).

- `module` — `name` из блока `probe` (обязательно)
- `target` — host, host:port или суффикс пути, добавляемый к scheme модуля (обязательно)
- `hostname` — опционально Host и TLS SNI (для IP-литерала в `target`)
- `X-Prometheus-Scrape-Timeout-Seconds` — опциональный заголовок; применяется только как таймаут **запущенной** проверки (минус 500 мс, не больше `timeout` модуля). `/probe` по-прежнему не ждёт.

Обработчик собирает полный URL (`http://`, `https://`, `tcp://`, `icmp://`, `ws://`, `unix://`, …) из `prober`, опционального `tls on` и `target`, запускает соответствующий агрегатор **асинхронно** (`smart_aggregator` + `aggregator_oneshot_start`) и сразу возвращает last-known метрики Prometheus для `host=<target>`. HTTP-ответ не ждёт окончания пробы (`aggregator_oneshot_await` здесь не используется). Первый scrape может быть пустым; следующий видит результат kick. Ключи kick — `probe:<module>:<base>`, чтобы не затирать scheduled `aggregate { blackbox }` того же хоста.

## Поля

| Поле | Описание |
|------|----------|
| `name` | Имя модуля для параметра `module=` (обязательно) |
| `prober` | `http`, `tcp`, `udp`, `icmp`, `dns`, `websocket` (`ws`) или `unix` (обязательно) |
| `tls` | `on` — HTTPS (`prober http` → `https://`), TLS (`prober tcp` → `tls://`), WSS (`prober websocket` → `wss://`) или `tls://unix:` (`prober unix`) |
| `timeout` | Таймаут проверки, применяется к async-работе (по умолчанию `5s`) |
| `method` | HTTP-метод: `GET` (по умолчанию), `POST`, `HEAD`, `PUT`, `DELETE`, `PATCH` |
| `body` | Тело HTTP-запроса |
| `url` | URL nameserver для `prober dns` (`udp://8.8.8.8:53`, `resolver://`, …) или схема unix (`http://unix:`, `unixgram://`) |
| `type` | Тип DNS-запроса для `prober dns` (`a`, `aaaa`, …; по умолчанию `a`) |
| `query_name` | Если задан — режим blackbox: `target` это **nameserver**, `query_name` — имя для резолва. По умолчанию Alligator наоборот (`target` = имя, `url` = сервер) |
| `valid_rcodes` | Допустимые DNS RCODE (`NOERROR`, `NXDOMAIN`, `0`, …). По умолчанию только `NOERROR` |
| `fail_if_answer_matches_regexp` / `fail_if_answer_not_matches_regexp` | Провал DNS-пробы, если объединённая answer-секция совпадает / не совпадает (PCRE, компиляция при загрузке модуля) |
| `unixgram` | `on` — datagram unix (`unixgram://`) вместо stream |
| `follow_redirects` | Лимит редиректов для HTTP(S) |
| `valid_status_codes` | Допустимые коды ответа (`2xx`, `3xx`, `101`, …). Для HTTP по умолчанию `2xx` |
| `fail_if_body_matches_regexp` / `fail_if_body_not_matches_regexp` | Провал HTTP-пробы, если тело совпадает / не совпадает (PCRE, компиляция при загрузке модуля) |
| `fail_if_header_matches` / `fail_if_header_not_matches` | PCRE-проверки заголовков (`header`, `regexp`, опционально `allow_missing`) |
| `fail_if_ssl` / `fail_if_not_ssl` | Провал, если TLS использовался / не использовался |
| `fail_if_json_not_exists` / `fail_if_json_matches_regexp` | jansson-валидаторы JSON-пути (`path` / `regexp`) |
| `query_response` | Диалог TCP/unix: `expect` (PCRE), `send`, `expect_bytes`, `starttls` |
| `negative_test` | Инверсия `probe_success` (успех, если проверка не прошла) |
| `payload_size` / `size` | Размер payload ICMP echo (байты) |
| `ttl` | ICMP IP TTL / IPv6 hop limit |
| `tos` | ICMP IP TOS / IPv6 traffic class |
| `interval` | Непрерывный ICMP: один echo на интервал; гистограмма RTT вместо залпа |
| `source_ip_address` | Адрес bind для async-клиента (TCP/UDP и raw-сокет ICMP, IPv4 и IPv6) |
| `probe_ip_version` / `preferred_ip_protocol` | `4`/`ip4` или `6`/`ip6` (семейство адресов ICMP) |
| `loop` | Повторить проверку N раз (статистика потерь ICMP; несколько UDP-пакетов в blackbox) |
| `percent` | Требуемая доля успехов при `loop` (0.0–1.0) |
| `payload` | Тело UDP-запроса и шаблон целостности: ответ должен содержать эту строку байт, иначе `probe_success=0` |
| `ca_file`, `cert_file`, `key_file`, `server_name` | TLS-клиент |
| `tls_verify` | `on` — проверять серверный сертификат |
| `http_proxy_url` / `proxy` | HTTP proxy для запроса |
| `env` / `header` | Дополнительные HTTP (и WebSocket upgrade) заголовки (`env=Header:value`) |
| `add_label` | Labels на emit-метриках при ingest; также на `/probe` OpenMetrics export. В значениях можно подставить `@target@` (полный `target` из `/probe`) или `@target.host@` (хост без порта/пути). Labels discovery GCE/K8s не подставляются |
| `metricstransform` | Перепись ключей/значений labels на ingest и на `/probe` export |
| `metric_name_transform_pattern` / `metric_name_transform_replacement` | PCRE-перепись имени на `/probe` OpenMetrics ответе |

## Примеры

ICMP с порогом потерь (из [`src/tests/system/blackbox/alligator.conf`](../../src/tests/system/blackbox/alligator.conf)):

```
probe {
    name icmp;
    prober icmp;
    timeout 5000;
    loop 10;
    percent 0.5;
}
```

`loop` пишет gauge последнего залпа `alligator_icmp_replies` / `alligator_icmp_losses` (при успешном `loop 10` это всегда `10` / `0`). Счётчики `alligator_icmp_replies_total`, `alligator_icmp_losses_total` и `alligator_icmp_echoes_total` увеличиваются на этот залп. В Prometheus blackbox_exporter таких метрик нет: один echo и `probe_success`.

HTTPS POST:

```
probe {
    name http_post_2xx;
    tls on;
    prober http;
    method POST;
    body '{"ok":true}';
    env X-Token:secret;
}
```

DNS (async resolver; `target` — имя для резолва), WebSocket и Unix:

```
probe {
    name dns_a;
    prober dns;
    url udp://8.8.8.8:53;
    type a;
}

probe {
    name ws_health;
    prober websocket;
}

probe {
    name unix_sock;
    prober unix;
}
```

TCP/UDP/unix stream выставляют `probe_success` на connect (или TLS handshake). С `query_response` успех — все шаги диалога. ICMP поддерживает IPv6-литералы `icmp://[::1]`. `GET /probe` остаётся async: первый scrape может быть пустым.

TCP SSH-баннер:

```
probe {
    name ssh_banner;
    prober tcp;
    query_response {
        expect "^SSH-2.0-";
        send "SSH-2.0-blackbox-ssh-check";
    }
}
```

DNS в режиме blackbox (`target` — nameserver):

```
probe {
    name dns_google;
    prober dns;
    query_name example.com;
    type a;
    valid_rcodes NOERROR NXDOMAIN;
}
```

UDP echo с проверкой payload и залпом из 3 пакетов (in-process echo-сервер не используется; удалённая сторона должна вернуть payload):

```
probe {
    name udp_echo;
    prober udp;
    payload alligator-probe;
    loop 3;
    percent 0.66;
}

curl 'http://127.0.0.1:1111/probe?module=udp_echo&target=127.0.0.1:7'
```

Regex по answer-секции DNS (провал, если нет A-записи `example.com`):

```
probe {
    name dns_example_a;
    prober dns;
    query_name example.com;
    type a;
    fail_if_answer_not_matches_regexp "example\\.com A ";
}
```

Подстановка `target` в `add_label` на `/probe` (и на ingest `probe_success`):

```
probe {
    name http_target;
    prober http;
    add_label instance:@target@ site:@target.host@;
}

curl 'http://127.0.0.1:1111/probe?module=http_target&target=example.com:443'
```

даёт labels `instance="example.com:443"` и `site="example.com"`.

## Метрики probe

Имена Alligator (`alligator_*`, `aggregator_*`, `x509_*`). Слоя алиасов blackbox_exporter нет. Уникальные `probe_*` остаются там, где нет native-двойника (на **следующем** scrape async `GET /probe`):

| Метрика | Смысл |
|---------|--------|
| `probe_success` | 1, если проверки модуля прошли (инверсия через `negative_test`) |
| `probe_failed_due_to_regex` | Выставляется при провале TCP/HTTP regex expect или body matcher |
| `probe_expect_info` | Совпал шаг `query_response` expect |
| `alligator_http_response_status_code` | HTTP status code |
| `alligator_http_response_body_bytes` | Размер HTTP-тела |
| `alligator_session_duration_seconds` | Длительность стадии сессии в секундах (`stage=connect|write|read|tls_handshake|tls_write|tls_read|shutdown|total`) |
| `alligator_dns_resolve_duration_seconds` | Длительность DNS resolve в секундах |
| `alligator_probe_timeout_seconds` | Настроенный timeout модуля |
| `alligator_probe_ip_protocol` | 4 или 6 по семейству сокета / getaddrinfo |
| `x509_cert_not_after` | notAfter TLS-сертификата (Unix seconds) |
| `alligator_icmp_rtt_seconds` | RTT последнего ICMP echo в секундах |
| `alligator_icmp_reply_hop_limit` | TTL/hop-limit последнего ответа (`type=icmp`, `host`) |
| `alligator_icmp_reply_ratio` / `alligator_icmp_loss_ratio` | Доля ответов/потерь последнего залпа **0–1** (не проценты) |

Непрерывный ICMP (`interval`) пишет гистограмму `alligator_icmp_response_duration_seconds_*`. HTTP — `alligator_http_request_duration_seconds_*`. Вне скоупа: gRPC, HTTP/2, HTTP/3, CEL, libjq, OAuth2.

## JSON-конфигурация

Массив `probe` описан в [api.md](api.md). Экспорт текущей конфигурации:

```
curl -s http://127.0.0.1:1111/conf
```

## Связанные разделы

- [blackbox parser](parsers/blackbox.md) — периодические проверки через aggregate
- [aggregate.md](aggregate.md) — TLS, proxy и revocation на blackbox URL
- [api.md](api.md) — HTTP endpoints
