**Language / Язык:** [English](../../mtail/configuration.md) | [Русский](configuration.md)

# Конфигурация Mtail

На этой странице описано, как настроить модуль `mtail` в Alligator.

## Обзор

Обычно mtail настраивается в двух местах:

1. Блок `mtail { ... }`: регистрирует и компилирует скрипты mtail
2. Блок `entrypoint { ... }`: принимает входные данные и назначает `handler mtail`

Оба варианта доступны через статическую конфигурацию или динамические payload API.

## Контекст `mtail`

Контекст `mtail` определяет именованную программу-скрипт.

### Обязательные поля

- `name` — уникальный ключ контекста
- `script` — путь к файлу скрипта mtail

### Необязательные поля

- `key` — необязательный пользовательский ключ (сохраняется с контекстом)
- `log_level_parser`
- `log_level_lexer`
- `log_level_generator`
- `log_level_compiler`
- `log_level_vm`
- `log_level_pcre`

Все поля `log_level_*` принимают обычные имена уровней логирования Alligator.

### Пример

```
mtail {
    name nginx_mtail;
    script /etc/alligator/mtail/nginx.mtail;
    log_level_vm debug;
}
```

## Интеграция с Entrypoint

Для обработки входящих payload с помощью mtail:

- установите `handler mtail`
- установите `mtail <name>`, чтобы привязать этот entrypoint к именованной программе mtail

### Минимальный пример

```
entrypoint {
    bind 0.0.0.0:19101;
    handler mtail;
    mtail nginx_mtail;
}
```

### Полезные необязательные поля Entrypoint

Часто полезны в пайплайнах mtail:

- `log_level` — детализация парсера/логов для runtime entrypoint
- `namespace` — префикс пространства имён метрик
- `env` — передача значений окружения, используемых при генерации запросов
- `allow` / `deny` — сетевые ACL
- `tls_certificate` / `tls_key` / `tls_ca` — настройки TLS-слушателя
- `mtail_full_export_interval` — как часто выполнять полный экспорт переменных mtail для **обновления TTL метрик** (см. ниже)

## `mtail_full_export_interval`

После каждого фрагмента ingest Alligator экспортирует переменные VM mtail в метрики. Для производительности обычно экспортируются только переменные, **затронутые** в этом фрагменте. Неактивным сериям с метками всё же нужен периодический **полный** экспорт, чтобы `metric_add` выполнялся для каждой переменной и обновлял TTL в expire-tree (та же идея, что и глобальный `ttl`).

Этот параметр задаёт **минимальный интервал в секундах по wall-clock** между такими полными экспортами.

### Поведение

- **По умолчанию:** если параметр опущен или задано недопустимое значение, интервал составляет **10 секунд** (то же, что и при отсутствии поля в JSON / plain config).
- **Допустимый диапазон:** `1` … `86400000` секунд (значения вне диапазона игнорируются; применяется значение по умолчанию).
- **Допустимые формы** (как у `ttl` entrypoint):
  - JSON **integer** или **real** (секунды).
  - JSON **string** с единицами длительности, например `"120s"`, `"2m"`, `"1h"` (разбирается через `get_sec_from_human_range`).

### Где задавать

| Источник | Как |
|--------|-----|
| Plain **entrypoint** | `mtail_full_export_interval 120;` (числовой токен, секунды) |
| Plain **aggregate** | `mtail_full_export_interval=120` внутри строки aggregate (тот же стиль `key=value`, что и `ttl=`) |
| API **entrypoint** JSON | `"mtail_full_export_interval": 120` или `"mtail_full_export_interval": "2m"` |
| API **aggregate** JSON | тот же ключ в объекте aggregate, передаваемом в `context_arg_json_fill` |

Значение сохраняется в **`context_arg`** парсера для этого entrypoint или aggregate, поэтому каждый поток может использовать свой интервал.

### Пример plain entrypoint

```
entrypoint {
    udp 0.0.0.0:5140;
    handler mtail;
    mtail myscript;
    mtail_full_export_interval 120;
}
```

### Пример API entrypoint

```
{
  "entrypoint": [
    {
      "udp": ["0.0.0.0:5140"],
      "handler": "mtail",
      "mtail": "myscript",
      "mtail_full_export_interval": "2m"
    }
  ]
}
```

### Пример aggregate (plain)

```
aggregate {
    mtail udp://127.0.0.1:5140 mtail_full_export_interval=90 name=myscript;
}
```

## Конфигурация через API

Контексты Mtail поддерживаются payload v1 API в секции `mtail`.

### Создание или обновление

```
{
  "mtail": [
    {
      "name": "nginx_mtail",
      "script": "/etc/alligator/mtail/nginx.mtail",
      "log_level_vm": "debug"
    }
  ]
}
```

### Удаление

Удаление использует ту же структуру, идентификация по `name`.

```
{
  "mtail": [
    {
      "name": "nginx_mtail"
    }
  ]
}
```

При HTTP `DELETE` Alligator вызывает внутренний путь удаления контекста mtail.

## Замечания и поведение

- Если контекст с тем же `name` уже существует, он заменяется.
- Если файл скрипта нельзя прочитать, создание контекста завершается ошибкой.
- Если для обработчика mtail нет пригодного скомпилированного контекста, статус парсера для этого payload помечается как failed.
- Значение `mtail` на уровне entrypoint сохраняется в имени контекста парсера и используется для поиска.

## Рекомендуемая структура

Для удобства сопровождения:

- храните скрипты в отдельном каталоге (например, `/etc/alligator/mtail/`)
- используйте стабильные имена контекстов (`service_parser`, `pipeline_parser` и т. п.)
- держите один логический скрипт на один контекст mtail

## Примеры конфигурации

### Пример 1: Aggregate с файловым источником и контекстом скрипта mtail

```
aggregate {
    mtail file:///var/log/maillog name=postfix log_level=info notify=only state=stream;
}

mtail {
    name postfix;
    script /etc/alligator/mtail/postfix.mtail;
}
```

Используйте, когда Alligator читает логи из файла и разбирает их программой mtail, выбранной через `name=postfix`.

### Пример 2: Несколько скриптов и выделенные порты

```
mtail {
    name nginx_access;
    script /etc/alligator/mtail/nginx_access.mtail;
}

mtail {
    name app_errors;
    script /etc/alligator/mtail/app_errors.mtail;
    log_level_vm debug;
}

entrypoint {
    bind 0.0.0.0:19101;
    handler mtail;
    mtail nginx_access;
    namespace web;
}

entrypoint {
    bind 0.0.0.0:19102;
    handler mtail;
    mtail app_errors;
    namespace app;
}
```

Используйте этот шаблон, когда у каждого потока логов своя грамматика и разбор должен выполнять отдельная программа mtail.

### Пример 3: Защищённая точка приёма mtail

```
mtail {
    name secure_logs;
    script /etc/alligator/mtail/secure_logs.mtail;
}

entrypoint {
    bind 0.0.0.0:19443;
    handler mtail;
    mtail secure_logs;
    tls_certificate /etc/alligator/tls/server.crt;
    tls_key /etc/alligator/tls/server.key;
    tls_ca /etc/alligator/tls/ca.crt;
    allow 10.10.0.0/16;
    deny 0.0.0.0/0;
}
```

Используйте для production-приёма, где только доверенные клиенты могут отправлять логи.

### Пример 4: Динамическая регистрация через API и обновление entrypoint

Создание/обновление контекстов mtail:

```
POST /api/v1/config
Content-Type: application/json

{
  "mtail": [
    {
      "name": "nginx_access",
      "script": "/etc/alligator/mtail/nginx_access.mtail"
    },
    {
      "name": "app_errors",
      "script": "/etc/alligator/mtail/app_errors.mtail",
      "log_level_vm": "debug"
    }
  ]
}
```

Настройка entrypoint, ссылающихся на них:

```
POST /api/v1/config
Content-Type: application/json

{
  "entrypoint": [
    {
      "bind": "0.0.0.0:19101",
      "handler": "mtail",
      "mtail": "nginx_access",
      "namespace": "web"
    },
    {
      "bind": "0.0.0.0:19102",
      "handler": "mtail",
      "mtail": "app_errors",
      "namespace": "app"
    }
  ]
}
```
