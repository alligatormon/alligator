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

Каждый ключ varnishstat становится `varnish_<key>`, например:

- `varnish_backend_*`, `varnish_cache_*`
- `varnish_*_bodybytes`
