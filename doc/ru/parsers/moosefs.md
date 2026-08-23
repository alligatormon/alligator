**Language / Язык:** [English](../../parsers/moosefs.md) | [Русский](moosefs.md)

## MooseFS

Запускает `mfscli` через `exec://` и разбирает статистику master/chunk server.

### Connection URL

```
exec:///path/to/mfscli
```

Alligator вызывает фиксированные аргументы: `-ns":" -SIM -SMU -SIG -SCS -SIC -SSC`.

### Пример

```
aggregate {
    moosefs exec:///usr/sbin/mfscli;
}
```

См. [`src/tests/mock/moosefs/alligator.conf`](../../../src/tests/mock/moosefs/alligator.conf).

### Metrics

- `moosefs_metadata_*`
- `moosefs_memory_info_*`
- `moosefs_chunk_matrix_*`
- `moosefs_chunk_server_*`
