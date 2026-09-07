**Language / Язык:** [English](postfix.md) | [Русский](../ru/parsers/postfix.md)

## Postfix queues

Walks a Postfix spool directory and counts files in `active`, `hold`, `incoming`, `deferred`, and `maildrop` (one level of hashed subdirectories is included).

Handler **`postfix`** treats the aggregate URL as a **directory root**, not as files to tail. `filetailer` does not crawl queue files: the parser walks the spool itself and uses `carg->host` as the spool path for `file://` URLs.

### Connection URL

```
file:///var/spool/postfix
file:///var/spool/postfix/
```

### Example

```
aggregate {
    postfix file:///var/spool/postfix;
}
```

The process needs read permission on the spool (often `postfix` group or root). Missing queue directories are skipped.

### Metrics

- `postfix_queue_length{queue}` — file count
- `postfix_queue_size_bytes{queue}` — sum of file sizes
- `postfix_queue_oldest_seconds{queue}` — age of the oldest file (`now - mtime`)

Unit tests: [`src/tests/unit2/parsers.h`](../../src/tests/unit2/parsers.h) (`api_test_parser_postfix`).
