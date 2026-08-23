**Language / Язык:** [English](../../parsers/monit.md) | [Русский](monit.md)

## Monit

Забирает полный статус Monit в XML и выставляет метрики сервисов.

### Connection URL

```
http://[user:pass@]host[:2812]
```

Порт HTTP Monit по умолчанию — **2812**.

### Пример

```
aggregate {
    monit http://user:password@localhost:2812;
}
```

### HTTP request

Alligator всегда запрашивает `/_status?format=xml&level=full`.

### Metrics

Динамические `monit_*` из XML, плюс:

- `monit_service_responsetime`
- `monit_program_start`
- `monit_timestamps_{access,change,modify}`
