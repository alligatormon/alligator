# Translating Alligator documentation

English documentation under `doc/` is the **source of truth**. Russian translations live in parallel under `doc/ru/` with the same filenames.

## Layout

| English | Russian |
|---------|---------|
| `README.md` | `README.ru.md` |
| `doc/foo.md` | `doc/ru/foo.md` |
| `doc/parsers/foo.md` | `doc/ru/parsers/foo.md` |
| `doc/images/` | shared — Russian pages use `../images/` or `../../images/` |

Each translated page starts with a language switcher:

```markdown
**Language / Язык:** [English](../aggregate.md) | [Русский](aggregate.md)
```

## Conventions

- Keep identifiers in English: context names (`aggregate`, `entrypoint`), config keys, metric names, URLs, file paths.
- Do not translate code blocks except comments.
- Fix unclear English in the source when translating if the meaning was wrong; update both languages in the same PR.
- Stub parser pages (minimal English content) are not translated until the English page is expanded.

## Maintenance

When you change an English page that has a Russian twin:

1. Update `doc/ru/…` in the **same pull request**, or
2. Add a one-line notice at the top of the Russian file: `> Перевод может быть устаревшим — см. английскую версию.`

To list English pages without a Russian counterpart:

```bash
find doc -name '*.md' ! -path 'doc/ru/*' ! -name 'TRANSLATING.md' | while read f; do
  ru="doc/ru/${f#doc/}"
  [ -f "$ru" ] || echo "missing: $ru"
done
```
