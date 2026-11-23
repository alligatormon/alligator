**Language / Язык:** [English](../../parsers/varnish.md) | [Русский](varnish.md)

## Varnish

Запускает `varnishstat -j` через `exec://` и выставляет значения JSON-счётчиков.

### Connection URL

```
exec:///usr/bin/varnishstat -j
```

Команда и флаг `-j` (JSON) должны быть в path после `exec://`.

### Пример

```
aggregate {
    varnish exec:///usr/bin/varnishstat -j;
}
```

См. [`src/tests/system/varnish/alligator.json`](../../../src/tests/system/varnish/alligator.json).

### Requirements

- Установленный `varnishstat` (пакет Varnish Cache)
- Права на чтение shared memory statistics

### Metrics

Каждый ключ varnishstat становится `varnish_<key>`. Counters (`flag=c`) получают суффикс `_total` и `TYPE counter`; gauges/bitmaps остаются gauges. HELP берётся из description varnishstat, если он есть.

Примеры:

- `varnish_cache_hit_total`, `varnish_backend_req_total`
- `varnish_n_object`, `varnish_threads` (gauges)
- `varnish_*_bodybytes_total`
