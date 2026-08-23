**Language / Язык:** [English](../../parsers/wazuh.md) | [Русский](wazuh.md)

# Wazuh

Обработчик aggregate `wazuh` читает state-файлы демонов Wazuh из каталога и экспортирует их как метрики Prometheus.

## Configuration

Укажите обработчику любой файл внутри каталога состояния Wazuh (обычно `/var/ossec/var/run/`):

```
aggregate {
    wazuh file:///var/ossec/var/run/wazuh-agentd.state;
}
```

Alligator читает соседние state-файлы из того же каталога:

| File | Metric prefix |
|------|----------------|
| `wazuh-agentd.state` | `wazuh_agentd_*` |
| `wazuh-remoted.state` | `wazuh_remoted_*` |
| `wazuh-analysisd.state` | `wazuh_analysisd_*` |
| `wazuh-logcollector.state` | JSON-метрики через `wazuh_logcollector` (см. ниже) |

## State file format

Каждая строка имеет вид `name='value'` или `name=value`. Строки-комментарии, начинающиеся с `#`, игнорируются.

Разбор значений:

- Метки времени в форме `YYYY-MM-DD HH:MM:SS` сохраняются как Unix time.
- Числовые строки сохраняются как unsigned integers.
- Состояния соединения `connected`, `pending` и `disconnected` сопоставляются с `1`, `2` и `0`.
- Другие строковые значения экспортируются как gauge `1` с меткой `type`, равной строковому значению.

## Exported metrics

Все метрики из `.state`-файлов имеют тип **gauge** с HELP-текстом `Wazuh API exported metric value.`

Examples:

```
# TYPE wazuh_agentd_status gauge
# HELP wazuh_agentd_status Wazuh API exported metric value.
wazuh_agentd_status 1
wazuh_remoted_queue_size 42
wazuh_analysisd_events_processed 123456
```

Имена метрик повторяют имена полей в state-файлах с префиксом демона (`wazuh_agentd_`, `wazuh_remoted_`, `wazuh_analysisd_`).

## Log collector JSON

`wazuh-logcollector.state` разбирается как JSON. Метрики экспортируются в семейство `wazuh_logcollector` с использованием настроенного JSON query path (по умолчанию `.global.files.[location]`).
