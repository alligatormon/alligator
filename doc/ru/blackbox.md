# Blackbox-проверки

**Language / Язык:** [English](../blackbox.md) | [Русский](blackbox.md)

Alligator умеет делать blackbox-проверки связности двумя способами. Списки полей — в [probe](probe.md) и [парсере blackbox](parsers/blackbox.md). Здесь — готовые конфиги для обоих режимов.

| Режим | Конфиг | Как выбираются цели |
|-------|--------|---------------------|
| **По запросу** | `entrypoint` + `probe { … }` | Prometheus (или скрипт) вызывает `GET /probe?module=…&target=…` |
| **По расписанию** | `entrypoint` + `aggregate { blackbox … }` | Фиксированный список URL на `aggregate_period` |

**Встроенных модулей probe нет.** Отсутствующий `module` на `/probe` даёт HTTP 400. `/probe` запускает проверку асинхронно и сразу отдаёт последние известные метрики; первый scrape новой цели часто пустой. Периодические проверки `aggregate` пишут в то же хранилище серий и видны на `GET /metrics`.

## По запросу (`GET /probe`)

Сохраните как `/etc/alligator.conf` (или передайте через `-c`) и перезапустите Alligator. Порт `9115` совпадает с обычным listen у blackbox_exporter.

```
entrypoint {
    handler prometheus;
    tcp 9115;
}

probe {
    name http_2xx;
    prober http;
    valid_status_codes 2xx;
    follow_redirects 5;
    timeout 5s;
}

probe {
    name https_2xx;
    prober http;
    tls on;
    valid_status_codes 2xx;
    follow_redirects 5;
    timeout 5s;
}

probe {
    name icmp;
    prober icmp;
    namespace probes;
    timeout 5s;
    loop 10;
    percent 0.5;
}

probe {
    name dns_a;
    prober dns;
    url udp://8.8.8.8:53;
    type a;
}

probe {
    name ssh_banner;
    prober tcp;
    query_response {
        expect "^SSH-2.0-";
        send "SSH-2.0-blackbox-ssh-check";
    }
}
```

`target` — host или `host:port`. Схему задают `prober` и опциональный `tls on`, а не строка target. Для HTTPS укажите `tls on` при `prober http`.

```
curl -sG 'http://127.0.0.1:9115/probe' \
  --data-urlencode 'module=http_2xx' \
  --data-urlencode 'target=example.com'

curl -sG 'http://127.0.0.1:9115/probe' \
  --data-urlencode 'module=https_2xx' \
  --data-urlencode 'target=example.com'

curl -sG 'http://127.0.0.1:9115/probe' \
  --data-urlencode 'module=icmp' \
  --data-urlencode 'target=8.8.8.8'

curl -sG 'http://127.0.0.1:9115/probe' \
  --data-urlencode 'module=dns_a' \
  --data-urlencode 'target=example.com'

curl -sG 'http://127.0.0.1:9115/probe' \
  --data-urlencode 'module=ssh_banner' \
  --data-urlencode 'target=github.com:22'
```

Другие модули (POST body, gRPC health, DNS `query_name`, UDP payload, матчеры заголовков): [probe](probe.md). Модули системного теста: [`src/tests/system/blackbox/alligator.conf`](../../src/tests/system/blackbox/alligator.conf).

## По расписанию (`aggregate { blackbox … }`)

Используйте, когда список URL зафиксирован в конфиге. Prometheus снимает `GET /metrics` с entrypoint; `/probe` не вызывается.

```
entrypoint {
    handler prometheus;
    tcp 1111;
}

aggregate_period 10s;
aggregate {
    blackbox http://example.com add_label=url:example.com;
    blackbox https://example.com add_label=url:example.com;
    blackbox tcp://google.com:80 add_label=url:google.com;
    blackbox tls://www.amazon.com:443 add_label=url:www.amazon.com;
    blackbox udp://8.8.8.8:53;
    blackbox icmp://8.8.8.8;
}
```

```
curl -s 'http://127.0.0.1:1111/metrics'
```

Схемы включают `http`, `https`, `tcp`, `tls`, `udp`, `icmp`, `ws`/`wss`, `grpc`/`grpcs`, `file`, `exec` и unix-сокеты. ICMP `loop` / непрерывный `interval`, WebSocket и gRPC health: [парсер blackbox](parsers/blackbox.md). TLS, proxy и общие опции aggregate: [aggregate](aggregate.md).

## Связанные документы

- [probe](probe.md) — поля `probe` и метрики `/probe`
- [парсер blackbox](parsers/blackbox.md) — URL и схемы в `aggregate`
- [приём метрик](metric-receive.md) — `/probe` в общем сценарии приёма
- [api](api.md) — HTTP-эндпоинты, включая `/probe`
