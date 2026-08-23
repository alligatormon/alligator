**Language / Язык:** [English](eventstore.md) | [Русский](../ru/parsers/eventstore.md)

## Event Store

Collects Event Store node statistics via HTTP JSON endpoints.

### Connection URL

```
http://host[:2113]
```

Default HTTP port is **2113**.

### Example

```
aggregate {
    eventstore http://localhost:2113;
}
```

### Options

Uses the `json_query` pipeline. Optional `pquery` / `jpath` fields on the aggregate line select JSON paths (see [json_query.md](json_query.md) and [aggregate.md](../aggregate.md)).

Default paths include queue, projection, and node state fields from `/stats`, `/projections/any`, and `/info`.

### Metrics

Emitted as `eventstore_*` via the JSON query handler.
