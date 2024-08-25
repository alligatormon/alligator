# Scheduler
This is a configuration parameter that enables the running of other software via an `action` or calling functions from other software through `lang` context.\
It permits the repeated running of a function at equal time intervals as specified in the `period` key.\
This directive can be specified multiple times. A single scheduler can only control the running one action or lang.

## Overview of configuration
```
scheduler {
  name <name>;
  period <1h15s>;
  datasource <internal>;
  action <run-script>;
  lang <call-func>;
  expr <filter promql>;
}
```

## name
Specifies only the context name.\
This does not affect anything in the context of collecting metrics; it is solely a key used to update fields of the scheduler context by the Alligator API.

## action
Specifies the action name to be executed.


## lang
Specifies the name of the language to be executed.


## expr
Specifies the PromQL expression to filter metrics that will be passed to a calling `lang` or a software in the `action` on the stdin.


## period
Specifies the time interval for the repeated execution of the command.\
More information about units that user can specify in configuratino can be obtained from configuration [doc](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md).


## datasource
Specifies the data source for the query.\
When making local metric requests, use the 'internal' key.\
Currently, only the `internal` option is supported.
