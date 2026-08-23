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

- `module` — `name` из блока `probe` (обязательно)
- `target` — host, host:port или суффикс пути, добавляемый к scheme модуля (обязательно)

Обработчик собирает полный URL (`http://`, `https://`, `tcp://`, `icmp://`, …) из `prober`, опционального `tls on` и `target`, выполняет blackbox parser один раз и возвращает метрики в текстовом формате Prometheus.

## Поля

| Поле | Описание |
|------|----------|
| `name` | Имя модуля для параметра `module=` (обязательно) |
| `prober` | `http`, `tcp`, `udp` или `icmp` (обязательно) |
| `tls` | `on` — HTTPS (`prober http` → `https://`) или TLS (`prober tcp` → `tls://`) |
| `timeout` | Таймаут проверки (по умолчанию `5s`) |
| `method` | `GET` или `POST` (только HTTP/HTTPS) |
| `follow_redirects` | Лимит редиректов для HTTP(S) |
| `valid_status_codes` | Допустимые коды ответа (`2xx`, `3xx`, `101`, …) |
| `loop` | Повторить проверку N раз (статистика потерь ICMP) |
| `percent` | Требуемая доля успехов при `loop` (0.0–1.0) |
| `ca_file`, `cert_file`, `key_file`, `server_name` | TLS-клиент |
| `tls_verify` | `on` — проверять серверный сертификат |
| `http_proxy_url` / `proxy` | HTTP proxy для запроса |
| `env` | Дополнительные HTTP-заголовки (`env=Header:value`) |
| `add_label` | Метки на emit-метриках |

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

HTTPS POST:

```
probe {
    name http_post_2xx;
    tls on;
    prober http;
    method POST;
}
```

## JSON-конфигурация

Массив `probe` описан в [api.md](api.md). Экспорт текущей конфигурации:

```
curl -s http://127.0.0.1:1111/conf
```

## Связанные разделы

- [blackbox parser](parsers/blackbox.md) — периодические проверки через aggregate
- [aggregate.md](aggregate.md) — TLS, proxy и revocation на blackbox URL
- [api.md](api.md) — HTTP endpoints
