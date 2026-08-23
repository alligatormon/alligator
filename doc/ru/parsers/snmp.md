**Language / Язык:** [English](../../parsers/snmp.md) | [Русский](snmp.md)

## SNMP

Чтобы опрашивать SNMP-агентов из Alligator, добавьте цели `snmp` в секцию `aggregate`. Реализация использует **SNMPv2c** (аутентификация community string). Community берётся из user-части URL; host и port идентифицируют агента; **query path** выбирает OID и тип запроса (GET, GET-NEXT или walk).

```
aggregate {
    # Single scalar (GET), e.g. sysUpTime.0
    snmp udp://public@192.0.2.1:161/1.3.6.1.2.1.1.3.0;

    # Walk a subtree (GET-NEXT chain); cap is compile-time (default 100000 steps)
    snmp udp://public@192.0.2.1:161/walk/1.3.6.1.2.1.2.2.1;

    # Optional GET-NEXT without walk prefix
    snmp udp://public@192.0.2.1:161/next/1.3.6.1.2.1.1.1.0;

    # Label series with a fixed mib= tag; shorten oid label to suffix under a prefix
    snmp udp://public@192.0.2.1:161/walk/1.3.6.1.2.1.25.3.2.1
        'env=snmp_mib:HOST-RESOURCES-MIB'
        'env=snmp_oid_strip_prefix:1.3.6.1.2.1.25'
        'env=snmp_friendly_names:1';

    # Load vendor MIBs for OID→symbol on OBJECT IDENTIFIER values (see below)
    snmp udp://public@192.0.2.1:161/1.3.6.1.2.1.1.2.0
        'env=snmp_mib_dirs:/usr/share/snmp/mibs:/opt/mibs';
}
```

Опции для каждой цели передаются как `env=key:value` в той же строке (см. `doc/aggregate.md`).

### URL path

- **`<dotted-oid>`** — SNMP GET для этого OID.
- **`walk/<dotted-oid>`** — Повторяющийся GET-NEXT, начиная после корня поддерева, пока поддерево не закончится или не будет достигнут лимит шагов walk.
- **`next/...`** или **`getnext/...`** — Один запрос GET-NEXT (та же механика walk без цикла).

Путь должен содержать непустой OID, кроме `walk/`, где OID поддерева следует за `walk/` (ведущие и завершающие слэши игнорируются).

### Environment variables (scrape context)

| Key | Role |
|-----|------|
| `snmp_mib` | Если задано, каждая экспортируемая метрика получает дополнительную метку `mib=<value>` (литеральная строка, не из файла). |
| `snmp_oid_strip_prefix` | Префикс dotted OID для удаления из метки `oid`; остаток отображается (удобно с `snmp_mib` для читаемости). |
| `snmp_friendly_names` | При `1` / `true` / `on` / `yes` известные OID сопоставляются со стабильными именами метрик, такими как `snmp_mib2_sys_up_time` или именами для таблиц (`snmp_hr_*`, вспомогательные TCP/UDP и т. д.), вместо generic-имён `snmp_scrape_*`. |
| `snmp_metric_from_oid` | При включении имена метрик выводятся из OID (с суффиксами вроде `_string` / `_missing` где применимо) вместо `snmp_scrape_*`. |
| `snmp_decode_tcp` | При `1` / `true` / `on` / `yes` индексы таблиц TCP (и связанных) декодируются в метки (`local_addr`, `local_port`, `rem_addr`, `rem_port`, `state` для состояния соединения). |
| `snmp_decode_tcp_metric_names` | По умолчанию включено; установите `0` / `false` / `off` / `no`, чтобы сохранять числовые scrape-метрики для TCP-таблиц даже при включённом `snmp_decode_tcp`. |
| `snmp_mib_dirs` | Список каталогов через двоеточие с определениями `*.mib` / `*.txt`. Используется для разрешения символов **MODULE-IDENTITY** для OID→name и для меток `symbol` на OBJECT IDENTIFIER octet strings. Перезагружается при изменении значения (см. `snmp_debug`). |
| `snmp_oid_symbols` | По умолчанию включено; установите `0` / `false` / `off` / `no`, чтобы отключить разрешение dotted-OID строк в известные символы модулей. |
| `snmp_debug` | `0` / unset — без дополнительных логов. `1` / `on` — перезагрузка MIB и обработка промахов. `2` / `verbose` / `all` — подробное логирование символов. Требуется debug log level на цели, чтобы видеть вывод `carglog`. |

### Metrics and labels

- Числовые ряды по умолчанию используют **`snmp_scrape_value`** с меткой **`oid`** (полный dotted OID или суффикс, если задан `snmp_oid_strip_prefix`). **`snmp_scrape_missing`** фиксирует `noSuchObject` / `noSuchInstance` с `reason`.
- Строковые / octet значения используют **`snmp_scrape_string`** с **`oid`** и **`value`** (и необязательной **`symbol`**, когда значение — dotted OID и включено разрешение символов). Некоторые OID получают специальное форматирование (например, `hrSystemDate`).
- При **`snmp_friendly_names`** строки таблиц могут добавлять **`hr_index`**, **`index`** или декодированные метки адресов TCP/UDP, как описано в коде (`snmp_friendly_metric_name_by_oid` и пути декодирования TCP).
- **`error_status`** агента экспортируется как **`snmp_error`** с метками `status=error_status`, `index=0`.
- **`snmp_mib`** добавляет метку **`mib`** ко всем метрикам этого scrape, если задано.

### MIB files

Необязательные каталоги MIB (`snmp_mib_dirs`) сканируются на определения объектов; обратная карта предпочитает OID **MODULE-IDENTITY**, чтобы имена символов оставались однозначными. Это дополняет встроенные таблицы OID→name, используемые для friendly-имён метрик и декодирования TCP/UDP.

### Limitations

- **SNMPv3** не реализован; только community-based v2c по UDP, как собирается в `snmp_mesg`.
- Глубина walk ограничена **`SNMP_WALK_MAX_STEPS`** (по умолчанию 100000, если не переопределено при сборке).
- Размер запроса/ответа ограничен (например, **2048** байт для собранного пакета); очень большие varbind могут не обрабатываться.
