
# Call external methods in different languages (now support duktape, lua, mruby and .so files (c/rust/c++)
```
lang {
    lang lua;
    method metrics_response;
    file /app/src/tests/system/lang/response.lua;
    arg "6";
    log_level 1;
    key lua_response;
}
```
