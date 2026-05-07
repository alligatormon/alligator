# Namespace

The `namespace` context defines metric namespaces and optional emission limits.

## Fields

- `name` (`string`) - namespace name.
- `max_emit` (`integer`, optional) - how many successful query emissions are allowed before metrics in this namespace are purged.

## `max_emit` behavior

- `0` or omitted: unlimited emissions, do not auto-delete by emission count.
- `1`: emit once, then purge all metrics in the namespace.
- `N > 1`: emit `N` times, then purge all metrics in the namespace.

Purging is triggered after successful serialization in internal query execution.

## JSON API example

```json
{
  "namespace": [
    { "name": "default", "max_emit": 0 },
    { "name": "oneshot", "max_emit": 1 },
    { "name": "burst", "max_emit": 3 }
  ]
}
```

## Plain configuration example

```conf
namespace {
    name default;
    max_emit 0;
}

namespace {
    name oneshot;
    max_emit 1;
}

namespace {
    name burst;
    max_emit 3;
}
```

## Notes

- Use non-negative integer values for `max_emit`.
- Negative values are not supported for "infinite" mode. Use `0` instead.
