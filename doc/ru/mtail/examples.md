**Language / Язык:** [English](../../mtail/examples.md) | [Русский](examples.md)

# Примеры Mtail

На этой странице собраны практические примеры использования mtail в Alligator.

## 1) Базовый счётчик строк

Скрипт mtail (`/etc/alligator/mtail/linecount.mtail`):

```
counter lines_total

/$/ {
  lines_total++
}
```

Конфигурация Alligator:

```
mtail {
    name linecount;
    script /etc/alligator/mtail/linecount.mtail;
}

entrypoint {
    bind 0.0.0.0:19101;
    handler mtail;
    mtail linecount;
}
```

Каждая принятая строка увеличивает `lines_total`.

## 2) Гистограмма по HTTP-коду

Скрипт mtail (`/etc/alligator/mtail/latency.mtail`):

```
histogram webserver_latency_by_code by code buckets 0, 1, 2, 4, 8

/latency=(?P<latency>\d+)s httpcode=(?P<httpcode>\d+)/ {
    webserver_latency_by_code [$httpcode] = $latency
}
```

Ожидаемые семейства метрик:

- `webserver_latency_by_code_bucket{le="..."}`
- `webserver_latency_by_code_sum`
- `webserver_latency_by_code_count`

## 3) Несколько программ Mtail

Можно зарегистрировать несколько контекстов и привязать их к разным entrypoint.

```
mtail {
    name nginx_access;
    script /etc/alligator/mtail/nginx_access.mtail;
}

mtail {
    name app_errors;
    script /etc/alligator/mtail/app_errors.mtail;
}

entrypoint {
    bind 0.0.0.0:19101;
    handler mtail;
    mtail nginx_access;
}

entrypoint {
    bind 0.0.0.0:19102;
    handler mtail;
    mtail app_errors;
}
```

## 4) Динамическая регистрация через API

Зарегистрировать контекст mtail во время работы:

```
POST /api/v1/config
Content-Type: application/json

{
  "mtail": [
    {
      "name": "linecount",
      "script": "/etc/alligator/mtail/linecount.mtail"
    }
  ]
}
```

Удалить контекст:

```
DELETE /api/v1/config
Content-Type: application/json

{
  "mtail": [
    { "name": "linecount" }
  ]
}
```

## 5) Советы по отладке

- Задайте уровни логирования стадий mtail (`log_level_vm`, `log_level_parser` и т. д.) в блоке `mtail {}`.
- Установите `log_level debug` на `entrypoint {}`, чтобы наблюдать поведение во время выполнения.
- Если метрики отсутствуют, проверьте:
  - путь к скрипту существует и доступен для чтения Alligator
  - `entrypoint.handler` равен `mtail`
  - `entrypoint.mtail` ссылается на существующее имя контекста mtail

## 6) Ожидания по формату входных данных

Обработчик mtail обрабатывает текстовые потоки построчно (разделитель `\n`):

- полные строки выполняются немедленно
- незавершённый хвост строки буферизуется
- буферизованный хвост дописывается к следующему входящему фрагменту

Это важно для доставки логов по TCP/HTTP chunked, когда записи могут разбиваться между пакетами.

## 7) Примеры скриптов upstream Google mtail

Готовые программы mtail см. в официальном каталоге примеров:

- https://github.com/google/mtail/tree/main/examples

Полезные стартовые скрипты:

- postfix: https://github.com/google/mtail/blob/main/examples/postfix.mtail
- linecount: https://github.com/google/mtail/blob/main/examples/linecount.mtail
- histogram: https://github.com/google/mtail/blob/main/examples/histogram.mtail
- mysql slow queries: https://github.com/google/mtail/blob/main/examples/mysql_slowqueries.mtail
- apache combined: https://github.com/google/mtail/blob/main/examples/apache_combined.mtail

Рекомендуемый рабочий процесс:

1. Скопируйте один скрипт в `/etc/alligator/mtail/`.
2. Адаптируйте regex и имена метрик под формат ваших логов.
3. Зарегистрируйте его в Alligator через `mtail { name ...; script ...; }`.
