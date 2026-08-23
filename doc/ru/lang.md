**Language / Язык:** [English](../lang.md) | [Русский](lang.md)

# Lang

Этот контекст вызывает экспортированную функцию из shared library (`.so` / `.dylib`),
загруженной через `modules`. Используйте его из C, C++, Go (`-buildmode=c-shared`) или Rust
(`cdylib`). Встроенные интерпретаторы (lua, mruby, duktape) удалены.

## key

Имя контекста. На него ссылаются scheduler, entrypoint и API.


## lang

Тип интерпретатора / загрузчика. Поддерживается только `so`.


## method

Имя экспортированного символа для вызова из shared library.


## file

Необязательный путь к script/data файлу, передаваемому в библиотеку (загружается в
`script` при запуске библиотеки).


## script

Необязательная inline-строка script/data, передаваемая в библиотеку.


## arg

Строка аргумента, передаваемая вызываемой функции.


## query

PromQL-выражение для сериализации метрик в вызов (см. `serializer`).


## hidden_arg

Если true, `arg` не экспортируется в API/конфигурацию.


## module

Имя записи в `modules { ... }`, указывающей на путь к `.so`.


## serializer

Сериализатор метрик, передаваемых в функцию:

- json
- dsv


## log_level

Уровень логирования для этого контекста. См.
[configuration.md](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-log-levels).


## C ABI

Загружаемый символ должен соответствовать:

```c
char *alligator_call(char *script, char *data, char *arg, char *metrics,
                     char *conf, char *parser_data, char *response,
                     char *queries);
```

Возвращает строку OpenMetrics (или другую, совместимую с multiparser), выделенную для
освобождения вызывающей стороной, или `NULL`.


## Пример

```
modules {
    go /tmp/liblang.so;
}

lang {
    log_level off;
    lang so;
    method alligator_call;
    arg 'go_metric';
    serializer json;
    query '';
    module go;
    key go;
}

scheduler {
    period 10s;
    lang go;
    name go;
}
```
