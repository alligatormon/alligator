**Language / Язык:** [English](../../parsers/nats.md) | [Русский](nats.md)

## nats.io

Чтобы включить сбор статистики с NATS, используйте следующую опцию:
```
aggregate {
    nats http://localhost:8222;
}
```

### Сбор статистики
NATS должен быть запущен с опцией `-m 8222`. Alligator опрашивает `/varz`, `/connz`, `/routez` и `/subsz`.

Парсер **не** превращает каждое JSON-поле в метрику: строковые поля больше не становятся gauge со значением `1` (`tls_version`, `lang`, …), а `/connz` отдаёт только агрегаты (без серий на каждый `cid`). `http_req_stats` — метрика `nats_varz_http_req_stats{endpoint="…"}`.

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
```
system {
    process nats-server;
    services nats-server.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="nats-server", src_port="4222"})';
	make socket_match;
	datasource internal;
}
```
