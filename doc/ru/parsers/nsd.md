**Language / Язык:** [English](../../parsers/nsd.md) | [Русский](nsd.md)

## NSD

Читает статистику NSD с control socket.

### Connection URL

```
unix:///run/nsd/nsd.ctl
```

### Пример

```
aggregate {
    nsd unix:///run/nsd/nsd.ctl;
}
```

См. [`src/tests/system/nsd/alligator.json`](../../../src/tests/system/nsd/alligator.json).

### Protocol

Alligator отправляет `NSDCT1 stats_noreset\n` на control socket и разбирает счётчики.

### Metrics

Примеры:

- `nsd_queries{type="…"}`
- `nsd_num*`, `nsd_num_{class,opcode,rcode}*`
