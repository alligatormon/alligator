**Language / Язык:** [English](../../parsers/apache-httpd.md) | [Русский](apache-httpd.md)

## Apache HTTP Server

### Сбор статистики
httpd предоставляет способ передачи статистики через [mod\_status](https://httpd.apache.org/docs/2.4/mod/mod_status.html).\
В конфигурационном файле должны быть указаны следующие опции:
```
ExtendedStatus On
<Location /server-status>
    SetHandler server-status
</Location>
```

Чтобы включить сбор статистики с httpd, используйте следующую опцию:
```
aggregate {
    httpd http://localhost/server-status;
    process 'exec:///usr/sbin/apachectl configtest';
}
```

Проверка `apachectl configtest` генерирует метрику из кода выхода httpd и создаёт две метрики о проверке конфигурации:
```
alligator_process_exit_status {proto="shell", key="exec:process:/usr/sbin/apachectl configtest:/", type="aggregator"} 0
alligator_process_term_signal {proto="shell", key="exec:process:/usr/sbin/apachectl configtest:/", type="aggregator"} 0
```

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
```
system {
    process httpd;
    services httpd.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="httpd", src_port="80"})';
	make socket_match;
	datasource internal;
}
```
