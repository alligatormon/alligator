**Language / Язык:** [English](../../parsers/beanstalkd.md) | [Русский](beanstalkd.md)

## Beanstalkd

Чтобы включить сбор статистики с beanstalkd, используйте следующую опцию:
```
aggregate {
    beanstalkd tcp://localhost:11300;
}
```

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
```
system {
    process beanstalkd;
    services beanstalkd.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="beanstalkd", src_port="11300"})';
	make socket_match;
	datasource internal;
}
```
