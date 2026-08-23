**Language / Язык:** [English](moosefs.md) | [Русский](../ru/parsers/moosefs.md)

## MooseFS

Runs `mfscli` via `exec://` and parses master/chunk server statistics.

### Connection URL

```
exec:///path/to/mfscli
```

Alligator invokes fixed arguments: `-ns":" -SIM -SMU -SIG -SCS -SIC -SSC`.

### Example

```
aggregate {
    moosefs exec:///usr/sbin/mfscli;
}
```

See [`src/tests/mock/moosefs/alligator.conf`](../../src/tests/mock/moosefs/alligator.conf).

### Metrics

- `moosefs_metadata_*`
- `moosefs_memory_info_*`
- `moosefs_chunk_matrix_*`
- `moosefs_chunk_server_*`
