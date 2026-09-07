**Language / Язык:** [English](../../parsers/postfix.md) | [Русский](postfix.md)

## Postfix queues

Обходит spool-каталог Postfix и считает файлы в `active`, `hold`, `incoming`, `deferred` и `maildrop` (включается один уровень hashed-подкаталогов).

Handler **`postfix`** трактует URL aggregate как **корень каталога**, а не как файлы для tail. `filetailer` не обходит queue files: parser сам ходит по spool и использует `carg->host` как путь spool для URL `file://`.

### Connection URL

```
file:///var/spool/postfix
file:///var/spool/postfix/
```

### Пример

```
aggregate {
    postfix file:///var/spool/postfix;
}
```

Процессу нужно право чтения spool (часто группа `postfix` или root). Отсутствующие каталоги очередей пропускаются.

### Metrics

- `postfix_queue_length{queue}` — число файлов
- `postfix_queue_size_bytes{queue}` — сумма размеров файлов
- `postfix_queue_oldest_seconds{queue}` — возраст самого старого файла (`now - mtime`)

Unit tests: [`src/tests/unit2/parsers.h`](../../../src/tests/unit2/parsers.h) (`api_test_parser_postfix`).
