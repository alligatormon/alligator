**Language / Язык:** [English](../../parsers/haproxy.md) | [Русский](haproxy.md)

## HAproxy

Собирает статистику HAProxy через admin socket (`show stat`, `show info`, pools, sessions).

### Connection URL

```
tcp://host[:port]
unix:///path/to/haproxy.sock
```

### Пример

По TCP:

```
aggregate {
    haproxy tcp://localhost:9999;
}
```

По unix socket:

```
aggregate {
    haproxy unix:///var/run/haproxy.sock;
}
```

### Metrics

Колонки CSV `show stat` и пары `show info` становятся метриками `haproxy_*` (строки frontend/backend/server и info процесса).
