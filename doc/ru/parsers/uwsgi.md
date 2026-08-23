**Language / Язык:** [English](../../parsers/uwsgi.md) | [Русский](uwsgi.md)

## uWSGI 

### Describe
uWSGI предоставляет [различные](https://uwsgi-docs.readthedocs.io/en/latest/StatsServer.html) способы передачи статистики.
Поддерживаются транспорт TCP и unix-socket, а также протоколы plain и HTTP.

### Сбор статистики
Сначала в конфигурации php-fpm должна быть указана опция pm.status\_path = /stats.

Чтобы включить сбор статистики с uWSGI, используйте следующую опцию:
### через TCP socket
```
aggregate {
    uwsgi tcp://localhost:1717;
}
```

### через TCP socket с опцией --stats-http
```
aggregate {
    uwsgi http://localhost:1717;
}
```

### через Unix socket
```
aggregate {
    uwsgi unix:///tmp/uwsgi.sock;
}
```

### через Unix socket с опцией --stats-http
```
aggregate {
    uwsgi http://unix:/tmp/uwsgi.sock;
}
```


Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
```
system {
    process uwsgi;
    services uwsgi.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="uwsgi", src_port="9090"})';
	make socket_match;
	datasource internal;
}
```
