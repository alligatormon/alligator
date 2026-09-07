**Language / Язык:** [English](../../parsers/nginx.md) | [Русский](nginx.md)

# Nginx

Open-source Nginx отдаёт классическую страницу **stub_status**. Alligator собирает её handler'ом **`nginx`**.

Включите в контексте `http` / `server`:

```
location /nginx_status {
    stub_status;
    allow 127.0.0.1;
    deny all;
}
```

Сбор:

```
aggregate {
    nginx http://127.0.0.1/nginx_status;
}
```

### Metrics

- `nginx_connections{state="active|reading|writing|waiting"}`
- `nginx_accepts_total`, `nginx_handled_total`, `nginx_requests_total`

Имя handler — **`nginx`** (исходник `src/parsers/nginx.c`). Unit tests: [`src/tests/unit2/parsers.h`](../../../src/tests/unit2/parsers.h) (`api_test_parser_nginx_stub_status`).

Полезно также мониторить процесс, systemd unit и открытые порты:

```
system {
    process nginx;
    services nginx.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="nginx", src_port="80"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="nginx", src_port="443"})';
	make socket_match;
	datasource internal;
}
```

Команда `nginx -t` возвращает статус проверки конфигурации:

```
aggregate {
	process 'exec:///sbin/nginx -t';
}
```

Это создаёт метрики по коду выхода:

```
alligator_process_exit_status {proto="shell", key="exec:process:/sbin/nginx -t:/", type="aggregator"} 0
alligator_process_term_signal {proto="shell", key="exec:process:/sbin/nginx -t:/", type="aggregator"} 0
```

## nginx upstream check module

Alligator также поддерживает сбор метрик из [upstream check module](https://github.com/yaoweibin/nginx_upstream_check_module), который широко используется для активных проверок работоспособности.

Следующая конфигурация должна быть указана в контексте server для включения передачи метрик:

```
location /status {
    check_status;
}
```

Чтобы включить сбор статистики из upstream check module, используйте следующую опцию:

```
aggregate {
	nginx_upstream_check http://localhost/status;
}
```

Handler **`nginx_upstream_check`** отделён от **`nginx`** (stub_status).

Для проверки сертификатов X509 в файловой системе см. описание в [контексте x509](https://github.com/alligatormon/alligator/blob/master/doc/x509.md).
