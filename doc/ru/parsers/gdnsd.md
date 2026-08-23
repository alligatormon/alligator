**Language / Язык:** [English](../../parsers/gdnsd.md) | [Русский](gdnsd.md)

## gdnsd

Читает статистику gdnsd с control socket в формате JSON.

### Connection URL

```
unix:///path/to/gdnsd/control.sock
tcp://unix:/path/to/gdnsd/control.sock
```

Parser отправляет 8-байтный заголовок `S` и разбирает JSON-ответ.

### Пример

```
aggregate {
    gdnsd unix:///usr/local/var/run/gdnsd/control.sock;
}
```

См. [`src/tests/system/gdnsd/alligator.yaml`](../../../src/tests/system/gdnsd/alligator.yaml).

### Options

Опциональные `pquery` / `jpath` на строке aggregate фильтруют JSON fields (см. [json_query.md](json_query.md)).

### Metrics

Примеры: `gdnsd_stats_noerror`, `gdnsd_udp_*`, `gdnsd_tcp_*` (префикс `gdnsd_`).
