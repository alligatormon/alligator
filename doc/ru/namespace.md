**Language / Язык:** [English](../namespace.md) | [Русский](namespace.md)

# Namespace

Контекст `namespace` задаёт пространства имён метрик и опциональные ограничения на отправку и число серий.

## Поля

- `name` (`string`) — имя namespace.
- `max_emit` (`integer`, optional) — сколько успешных отправок query разрешено, прежде чем метрики в этом namespace будут удалены.
- `max_series` (`integer`, optional) — максимум живых time series в namespace. `0` — без лимита. Если поле опущено при обновлении, прежний кап не меняется. Новые серии сверх лимита отклоняются; обновления существующих проходят. Отказы считаются в `alligator_series_dropped_total{namespace="<name>"}` (публикуется в default namespace).

## Поведение `max_emit`

- `0` или опущено: неограниченное число отправок, автоматическое удаление по счётчику отправок не выполняется.
- `1`: отправить один раз, затем удалить все метрики в namespace.
- `N > 1`: отправить `N` раз, затем удалить все метрики в namespace.

Удаление срабатывает после успешной сериализации при внутреннем выполнении query.

## Поведение `max_series`

- Независимо от `max_emit`.
- Считаются серии, хранящиеся в metrictree (`metrictree->count`).
- Просроченные серии освобождают слоты.
- Flush счётчика отказов использует внутренний bypass-флаг (не освобождение по имени метрики), чтобы push-клиенты не обходили кап подделкой `alligator_series_dropped_total`.

## Пример JSON API

```json
{
  "namespace": [
    { "name": "default", "max_emit": 0, "max_series": 0 },
    { "name": "oneshot", "max_emit": 1 },
    { "name": "burst", "max_emit": 3 },
    { "name": "push", "max_series": 100000 }
  ]
}
```

## Пример plain-конфигурации

```conf
namespace {
    name default;
    max_emit 0;
}

namespace {
    name oneshot;
    max_emit 1;
}

namespace {
    name burst;
    max_emit 3;
}

namespace {
    name push;
    max_series 100000;
}
```

## Примечания

- Для `max_emit` и `max_series` используйте неотрицательные целые значения.
- Отрицательные значения не поддерживаются для режима «бесконечно». Вместо них используйте `0`.
