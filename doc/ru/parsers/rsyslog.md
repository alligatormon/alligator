**Language / Язык:** [English](../../parsers/rsyslog.md) | [Русский](rsyslog.md)

# rsyslog impstats

Обработчик entrypoint `rsyslog-impstats` разбирает сообщения [rsyslog impstats](https://www.rsyslog.com/doc/configuration/modules/impstats.html), пересылаемые по UDP или TCP.

## Configuration

```
entrypoint {
    handler rsyslog-impstats;
    udp 127.0.0.1:1111;
}
```

## Настройка rsyslog

Пример фрагмента `rsyslog.conf`:

```
module(
    load="impstats"
    interval="10"
    resetCounters="off"
    log.file="off"
    log.syslog="on"
    ruleset="rs_impstats"
)

template(name="impformat" type="list") {
    property(outname="message" name="msg")
}

ruleset(name="rs_impstats" queue.type="LinkedList" queue.filename="qimp" queue.size="5000" queue.saveonshutdown="off") {
    *.* action (
        type="omfwd"
        target="127.0.0.1"
        port="1111"
        protocol="udp"
        Template="impformat"
    )
}
```

Каждое сообщение impstats разбирается в один или несколько образцов метрик. Парсер извлекает имя модуля, origin, необязательное action и числовые значения по ключам из заголовка и тела impstats.

## Exported metrics

| Metric | Type | Description |
|--------|------|-------------|
| `rsyslog_stats` | gauge | Числовой счётчик или rate impstats для комбинации module/origin/key. |

### Labels

| Label | Description |
|-------|-------------|
| `module` | Имя модуля rsyslog из заголовка impstats. |
| `origin` | Поле origin из заголовка impstats (`origin=...`). |
| `action` | Имя action, если присутствует в заголовке; иначе опускается. |
| `key` | Ключ статистики из тела impstats. |

### Example output

```
# TYPE rsyslog_stats gauge
# HELP rsyslog_stats Rsyslog impstats value by module, origin, action, and key.
rsyslog_stats{module="core.action",origin="core.action",key="processed"} 42
```
