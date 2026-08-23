**Language / Язык:** [English](../../parsers/json_query.md) | [Русский](json_query.md)

## Json Query

Чтобы генерировать метрики из JSON, используйте следующую опцию:
```
aggregate {
    json_query https://example.com/api/v1/stats;
}
```

### Параметры pquery
Этот параметр можно использовать несколько раз. Он позволяет выполнять запросы для разбора JSON. Если JSON-парсер находит список, он может использовать ключ, указанный в квадратных скобках [], как метку метрики.
```
aggregate {
	json_query file:///root//test.json log_level=debug;
	json_query file:///root//test1.json "pquery=.[timestamp].data | .[type]" "pquery=.[timestamp].included.[id]";
}
```

### Поддержка jq-подобных pipeline
`pquery` теперь поддерживает jq-подобное связывание стадий и ветвление:
- `|` переходит к следующей стадии.
- `,` создаёт ветви на той же стадии.
- `.field` углубляется в JSON.
- `[field]` извлекает метку из текущего JSON-объекта (корневого объекта или каждого элемента массива).
- `[field:label]` (или `[field=label]`) извлекает поле и переименовывает метку.
- Блоки меток поддерживают пробелы или запятые между элементами.

Для построчных JSON-объектов (JSON Lines / NDJSON, например PostgreSQL `log_destination = jsonlog`) используйте корневой блок меток. Когда буфер не является валидным единым JSON-документом, `json_query` разбирает его построчно (один объект на строку):

```
aggregate {
    json_query file:///var/log/postgresql/json.log \
        "pquery=.[message dbname user error_severity state_code ps backend_type session_id remote_host timestamp]";
}
```

Каждая непустая строка должна быть полным JSON-объектом. При чтении хвоста файла фрагмент чтения может разрезать строку посередине; такой фрагмент пропускается, пока следующее чтение не предоставит полную строку (то же ограничение, что и у других stream-парсеров без буферизации строк).

Примеры:
```
aggregate {
	# Старый стиль, по-прежнему валиден:
	json_query file:///root/test1.json "pquery=.[timestamp].included.[id]";

	# Две ветви на одной стадии:
	json_query file:///root/test1.json "pquery=.[timestamp] | .data, .included | .[id,type]";

	# Псевдонимы меток:
	json_query file:///root/test1.json "pquery=.[timestamp:ts] | .included | .[id:entity_id type:kind]";

	# Несколько pquery объединяются:
	json_query file:///root/test1.json \
		"pquery=.[timestamp] | .data | .[type]" \
		"pquery=.[timestamp] | .included | .[id]";
}
```
