**Language / Язык:** [English](../../parsers/eventstore.md) | [Русский](eventstore.md)

## Event Store

Собирает статистику Event Store через HTTP JSON endpoints.

### Connection URL

```
http://host[:2113]
```

Порт HTTP по умолчанию — **2113**.

### Пример

```
aggregate {
    eventstore http://localhost:2113;
}
```

### Options

Использует pipeline `json_query`. Опциональные `pquery` / `jpath` на строке aggregate выбирают JSON paths (см. [json_query.md](json_query.md) и [aggregate.md](../aggregate.md)).

По умолчанию читаются queue, projection и state из `/stats`, `/projections/any`, `/info`.

### Metrics

Эмитятся как `eventstore_*` через JSON query handler.
