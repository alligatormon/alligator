# Namespace

The `namespace` context defines metric namespaces and optional emission / series limits.

## Fields

- `name` (`string`) - namespace name.
- `max_emit` (`integer`, optional) - how many successful query emissions are allowed before metrics in this namespace are purged.
- `max_series` (`integer`, optional) - maximum number of live time series allowed in this namespace. `0` means unlimited. Omitted on update leaves the existing cap unchanged. New series past the cap are refused; updates to existing series still succeed. Refusals increment `alligator_series_dropped_total{namespace="<name>"}` (published in the default namespace).

## `max_emit` behavior

- `0` or omitted: unlimited emissions, do not auto-delete by emission count.
- `1`: emit once, then purge all metrics in the namespace.
- `N > 1`: emit `N` times, then purge all metrics in the namespace.

Purging is triggered after successful serialization in internal query execution.

## `max_series` behavior

- Independent of `max_emit`.
- Counted as distinct series currently stored in the metrictree (`metrictree->count`).
- Expired series free slots for new ones.
- The drop-counter flush uses an internal bypass flag (not a public metric-name exemption) so push clients cannot inflate a capped namespace by forging `alligator_series_dropped_total`.

## JSON API example

```json
{
  "namespace": [
    { "name": "default", "max_emit": 0, "max_series": 0 },
    { "name": "oneshot", "max_emit": 1 },
    { "name": "burst", "max_emit": 3 },
    { "name": "push", "max_series": 100000 }
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

namespace {
    name push;
    max_series 100000;
}
```

## Notes

- Use non-negative integer values for `max_emit` and `max_series`.
- Negative values are not supported for "infinite" mode. Use `0` instead.
