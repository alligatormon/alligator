**Language / Язык:** [English](../../parsers/auditd.md) | [Русский](auditd.md)

# auditd

Обработчик entrypoint `auditd` разбирает записи audit-лога Linux в нативном формате `key=value` и экспортирует их как метрики Prometheus.

Каждая непустая входная строка увеличивает счётчик `auditd_event` на единицу. Выбранные поля audit становятся метками метрик.

Этот обработчик **считает события audit-лога**. Он не разбирает статус демона auditd из `auditctl -s` и внутреннюю статистику очереди.

## Конфигурация

```
entrypoint {
    handler auditd;
    tcp 127.0.0.1:1514;
}
```

Также поддерживается UDP:

```
entrypoint {
    handler auditd;
    udp 127.0.0.1:1514;
}
```

Обработчик ожидает, что тело сообщения содержит одну или несколько audit-записей, разделённых переводами строк. Пустые строки игнорируются.

## Формат входных данных

Типичная audit-запись выглядит так:

```
type=SYSCALL msg=audit(1710000000.123:456): arch=c000003e syscall=59 success=yes exit=0 auid=1000 uid=1000 gid=1000 exe="/usr/bin/bash" comm="bash" key="my-rule"
```

Правила разбора:

- Поля разбираются как пары `key=value`.
- Значения в двойных и одинарных кавычках могут содержать пробелы.
- Токены без `=` (например, syslog-префикс) пропускаются.
- Только подмножество audit-полей экспортируется как метки (см. ниже).

## Экспортируемые метрики

| Metric | Type | Description |
|--------|------|-------------|
| `auditd_event` | counter | Количество полученных записей audit-лога. |

### Метки

| Label | Source audit fields | Notes |
|-------|---------------------|-------|
| `type` | `type` | Тип события, например `SYSCALL`, `EXECVE`, `USER_AUTH`. |
| `success` | `success` | Обычно `yes` или `no`. |
| `exe` | `exe` | Путь к исполняемому файлу. Поддерживаются значения в кавычках с пробелами. |
| `key` | `key` | Ключ audit-правила, например `my-rule` или `(null)`. |
| `AUID` | `auid` or `AUID` | Сохраняется как `AUID`. |
| `UID` | `uid` or `UID` | Сохраняется как `UID`. |
| `GID` | `gid` or `GID` | Сохраняется как `GID`. |

Пример exposition:

```
# HELP auditd_event Number of audit log events received, labeled by event attributes.
# TYPE auditd_event counter
auditd_event{type="SYSCALL",success="yes",exe="/usr/bin/bash",key="my-rule",AUID="1000",UID="1000",GID="1000"} 42
```

Используйте `rate()` или `increase()` для этого счётчика, чтобы измерять частоту событий по комбинациям меток.

## Пересылка audit-логов через rsyslog

Типичная схема — пересылка строк `/var/log/audit/audit.log` в Alligator по TCP или UDP.

Пример rsyslog:

```
module(load="imfile" PollingInterval="10")

input(type="imfile"
      File="/var/log/audit/audit.log"
      Tag="audit:"
      Severity="info"
      Facility="local6")

action(type="omfwd"
       Target="127.0.0.1"
       Port="1514"
       Protocol="tcp"
       Template="RSYSLOG_TraditionalForwardFormat")
```

Настройте entrypoint Alligator на `127.0.0.1:1514` с `handler auditd`.

Если пересылаемое сообщение содержит syslog-заголовок, парсер пропускает токены, не являющиеся `key=value`, и всё равно извлекает audit-поля из той же строки.

## Ограничения

- Одна метрика на строку audit; связанные многострочные записи (`PATH`, `CWD`, `PROCTITLE`) считаются отдельно.
- Большинство audit-полей (`syscall`, `pid`, `comm`, `arch` и другие) не экспортируются как метки.
- Значения меток длиннее 255 символов обрезаются.
