**Language / Язык:** [English](monit.md) | [Русский](../ru/parsers/monit.md)

## Monit

Fetches Monit full status as XML and exposes service metrics.

### Connection URL

```
http://[user:pass@]host[:2812]
```

Default Monit HTTP port is **2812**.

### Example

```
aggregate {
    monit http://user:password@localhost:2812;
}
```

### HTTP request

Alligator always requests `/_status?format=xml&level=full`.

### Metrics

Dynamic `monit_*` metrics from XML service entries, plus:

- `monit_service_responsetime`
- `monit_program_start`
- `monit_timestamps_{access,change,modify}`
