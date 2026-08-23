**Language / Язык:** [English](../../parsers/lighttpd.md) | [Русский](lighttpd.md)

## Lighttpd

### Сбор статистики
Lighttpd предоставляет способ передачи статистики через [Mod\_status](https://redmine.lighttpd.net/projects/lighttpd/wiki/Mod_status).\
В конфигурации Lighttpd должны быть указаны опции `status.status-url` и `status.statistics-url`.

Чтобы включить сбор статистики с Lighttpd, используйте следующую опцию:
```
aggregate {
    beanstalkd tcp://localhost:11300;
    process 'exec:///usr/sbin/lighttpd -tt -f /etc/lighttpd/lighttpd.conf';
}
```

Проверка `lighttpd -tt` генерирует метрику из кода выхода lighttpd и создаёт две метрики о проверке конфигурации:
```
alligator_process_exit_status {proto="shell", key="exec:process:/usr/sbin/lighttpd -tt -f /etc/lighttpd/lighttpd.conf:/", type="aggregator"} 0
alligator_process_term_signal {proto="shell", key="exec:process:/usr/sbin/lighttpd -tt -f /etc/lighttpd/lighttpd.conf:/", type="aggregator"} 0
```

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
```
system {
    process lighttpd;
    services lighttpd.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="lighttpd", src_port="80"})';
	make socket_match;
	datasource internal;
}
```
