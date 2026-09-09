**Language / Язык:** [English](../../parsers/freeradius.md) | [Русский](freeradius.md)

## FreeRADIUS

Собирает статистику FreeRADIUS: authentication, accounting, proxy и очереди.

Имя handler — **`freeradius`**. Два входа:

1. **RADIUS Status-Server** по UDP (как Telegraf `inputs.freeradius`). User или password в URL — shared secret. Порт status по умолчанию **18121**.
2. **Текст** `radmin stats` или дамп атрибутов `radclient` (`exec://`). `mesg` пустой: агрегатор передаёт stdout в parser.

Для UDP в `radiusd.conf` нужен `status_server = yes` и status-клиент с секретом.

### Connection URL

```
udp://adminsecret@127.0.0.1:18121
udp://:adminsecret@127.0.0.1:18121
exec://radmin
```

### Пример

Status-Server (секрет как user URL, в стиле SNMP community):

```
aggregate {
    freeradius udp://adminsecret@127.0.0.1:18121;
}
```

Через `radmin`:

```
aggregate {
    freeradius 'exec://radmin -q stats';
}
```

Нестандартный control socket:

```
aggregate {
    freeradius 'exec://radmin -f /var/run/radiusd/radiusd.sock -q stats';
}
```

FreeRADIUS 3.2+ может отдавать Prometheus по HTTP; его можно снимать через [`prometheus_metrics`](prometheus_metrics.md).

### Metrics

Счётчики (примеры): `freeradius_access_requests_total`, `freeradius_access_accepts_total`, `freeradius_access_rejects_total`, `freeradius_accounting_requests_total`, proxy-варианты.

Gauges: `freeradius_queue_len_{internal,proxy,auth,acct,detail}`, `freeradius_start_time_seconds`, `freeradius_hup_time_seconds`.

Unit tests: [`src/tests/unit2/parsers.h`](../../../src/tests/unit2/parsers.h) (`api_test_parser_freeradius`).
