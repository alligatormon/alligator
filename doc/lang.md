# Lang
This context allows calling external functions in different programming languages. Now support duktape, lua, mruby and .so files (c/rust/c++).

## key
Specifies the name of the context. It can be used as a reference for the scheduler, or in the API.


## lang
Specifies the interpreter for calling functions.
Available values:
- lua
- mruby
- duktape
- so


## method 
Specifies the method or function to call.


## file
Specifies the file path to the script.


## script
This option allows the usage of inline scripts. It is useful for small scripts.


## arg
Specifies the argument of the function that will be passed to a calling function


## query
Specifies the PromQL expression to filter metrics that will be passed to a calling function.


## hidden\_arg
If true, then argument will be hidden on API requests.


## module
Specifies the module name to run for the `so` interpret.


## serializer
Specifies the serializer of input metrics to the function.
Available values:
- json
- dsv


## log\_level
Specify of the level of logging for this context. Units for this option are explained in this [document](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-log-levels)


## Overview
The examples of usage will be provided in the documentation, which will appear soon.

```
lang {
    lang lua;
    method metrics_response;
    file /usr/bin/libexec/alligator/response.lua;
    arg "6";
    log_level info;
    key lua_response;
}

lang {
    lang lua;
    method alligator_metrics;
    script 'function alligator_metrics(arg) return arg .. " 12"; end';
    arg "lua_metrics";
    log_level off;
    key lua;
}

lang {
    lang lua;
    method alligator_metrics;
    query '';
    file /usr/libexec/alligator/script.lua;
    arg "6";
    log_level off;
    key lua-file;
}

lang {
    lang lua;
    method metrics_response;
    file /usr/libexec/alligator/response.lua;
    arg "6";
    log_level off;
    key lua_response;
}

scheduler {
    period 10s;
    lang mruby2;
    name mruby2;
}

lang {
    lang mruby;
    log_level 0;
    method test_mruby;
    script 'def test_mruby(arg, metrics, conf, parser_data) alligator_set_metrics("test_mruby_from_conf 1313") end;';
    arg mruby_metrics;
    key mruby;
}

scheduler {
    period 10s;
    lang mruby;
    name mruby;
}

lang {
    lang mruby;
    method mruby_method;
    log_level off;
    file /usr/libexec/alligator/mruby.rb;
    arg mruby2_metrics;
    key mruby2;
}

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

lang {
    lang duktape;
    method add_metric;
    log_level off;
    query '';
    conf on;
    arg 'duktape_metric 15';
    script "function add_metric(text, metrics, conf) { return text;};";
    key duktape;
}

scheduler {
    name duktape2;
    period 1s;
    lang duktape2;
}

lang {
    lang duktape;
    method run;
    log_level off;
    arg 'duktape2';
    file /usr/bin/libexec/alligator/duktape.js;
    key duktape2;
}
```
