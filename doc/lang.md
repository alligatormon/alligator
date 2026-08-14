# Lang

This context calls an exported function from a shared library (`.so` / `.dylib`)
loaded via `modules`. Use it from C, C++, Go (`-buildmode=c-shared`), or Rust
(`cdylib`). Embedded interpreters (lua, mruby, duktape) were removed.

## key

Name of the context. Referenced by schedulers, entrypoints, and the API.


## lang

Interpreter / loader type. Only `so` is supported.


## method

Exported symbol name to call from the shared library.


## file

Optional path to a script/data file passed into the library (loaded into
`script` when the library is run).


## script

Optional inline script/data string passed to the library.


## arg

Argument string passed to the called function.


## query

PromQL expression used to serialize metrics into the call (see `serializer`).


## hidden_arg

If true, `arg` is omitted from API/config export.


## module

Name of the entry under `modules { ... }` that points at the `.so` path.


## serializer

Serializer for metrics passed into the function:

- json
- dsv


## log_level

Logging level for this context. See
[configuration.md](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-log-levels).


## C ABI

The loaded symbol must match:

```c
char *alligator_call(char *script, char *data, char *arg, char *metrics,
                     char *conf, char *parser_data, char *response,
                     char *queries);
```

Return an OpenMetrics (or other multiparser-compatible) string allocated for
the caller to free, or `NULL`.


## Example

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
