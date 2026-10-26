**Language / Язык:** [English](../../parsers/blackbox.md) | [Русский](blackbox.md)

## Blackbox в aggregate

Чтобы включить сбор blackbox-статистики, используйте следующую опцию:
```
aggregate {
    #HTTP checks:
    blackbox  http://example.com;
    #ICMP checks:
    blackbox icmp://example.com;
    #BASH exec shell:
    blackbox exec:///bin/curl http://example.com:1111/metrics;

    # Blackbox checks
    blackbox tcp://google.com:80 add_label=url:google.com;
    blackbox tls://www.amazon.com:443 add_label=url:www.amazon.com;
    blackbox udp://8.8.8.8:53;
    blackbox http://yandex.ru;
    blackbox https://nova.rambler.ru/search 'env=User-agent:googlebot';

    # file stat calc:
    blackbox file:///etc/ checksum=murmur3 file_stat=true calc_lines=true;

    # WebSocket connectivity check (handshake + stream):
    blackbox ws://api.example.com:8080/health  add_label=service:api;
    blackbox wss://ws.example.com/status       add_label=service:ws-gateway;

    # gRPC health (grpc.health.v1.Health/Check), не проверка TCP connect.
    # service= выбирает gRPC-сервис; без него — проверка всего сервера.
    # Порт по умолчанию 80 для grpc:// и 443 для grpcs://.
    blackbox grpc://127.0.0.1:50051;
    blackbox grpc://api.example.com:50051 service=foo.Bar;
    blackbox grpcs://api.example.com:443 service=foo.Bar tls_verify=on tls_server_name=api.example.com;
}
```

ICMP `loop` (или aggregate `pingloop`) перезаписывает gauge последнего залпа `alligator_icmp_replies` / `alligator_icmp_losses`. Счётчики `alligator_icmp_replies_total`, `alligator_icmp_losses_total` и `alligator_icmp_echoes_total` копятся между залпами. Prometheus blackbox_exporter шлёт один echo и не имеет packet counters.

Непрерывный ICMP в стиле smokeping_prober использует `interval` вместо залпа:

```
aggregate {
    blackbox icmp://8.8.8.8 interval=1000;
}
```

Пишется гистограмма `alligator_icmp_response_duration_seconds_{bucket,sum,count}`. На том же объекте aggregate принимаются опциональные `payload_size`, `ttl`, `tos`, `negative_test`. Проверки по запросу остаются на **`probe`** и async `GET /probe` (см. [probe.md](../probe.md)).

`grpc://` и `grpcs://` выполняют унарный `grpc.health.v1.Health/Check`. `probe_success` равен 1 только когда gRPC status равен 0 и статус здоровья — `SERVING`. Успешный TCP connect сам по себе успехом не считается. `service=` задаёт имя gRPC-сервиса (поле 1 protobuf); без него проверка серверная. `grpcs://` включает TLS (порт по умолчанию 443). `grpc://` остаётся plaintext (порт по умолчанию 80).

### Blackbox в entrypoint

Проверки по запросу используют модули **`probe`** и `GET /probe?module=…&target=…` вместо периодического `aggregate`. См. [probe.md](../probe.md).

Пример:

```
entrypoint {
    handler prometheus;
    tcp 1111;
}

probe {
    name http_2xx;
    prober http;
    valid_status_codes 2xx;
}
```

```
curl 'http://127.0.0.1:1111/probe?module=http_2xx&target=example.com'
```

`http_2xx` не встроен: сначала задайте блок `probe`, иначе `/probe` вернёт HTTP 400.
