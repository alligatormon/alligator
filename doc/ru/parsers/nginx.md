**Language / Язык:** [English](../../parsers/nginx.md) | [Русский](nginx.md)

# Nginx

Версия Nginx с открытым исходным кодом не предоставляет метрики. Рекомендуется использовать модуль [VTS](https://github.com/vozlt/nginx-module-vts/tree/master) для получения метрик.

Кроме того, полезно мониторить статистику процесса, запущенные сервисы и открытые порты со следующей конфигурацией:
```
system {
    process nginx;
    services nginx.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="nginx", src_port="80"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="nginx", src_port="443"})';
	make socket_match;
	datasource internal;
}
```


Команда `nginx -t` возвращает статус проверки конфигурации и формирует следующую метрику:
```
aggregate {
	process 'exec:///sbin/nginx -t';
}
```
Это создаст метрику на основе кода выхода Nginx и две метрики о проверке конфигурации:
```
alligator_process_exit_status {proto="shell", key="exec:process:/sbin/nginx -t:/", type="aggregator"} 0
alligator_process_term_signal {proto="shell", key="exec:process:/sbin/nginx -t:/", type="aggregator"} 0
```

## nginx upstream check module
Alligator также поддерживает сбор метрик из [upstream check module](https://github.com/yaoweibin/nginx_upstream_check_module), который широко используется для активных проверок работоспособности.

Следующая конфигурация должна быть указана в контексте server для включения передачи метрик:
```
location /status {
    check_status;
}
```

Чтобы включить сбор статистики из upstream check module, используйте следующую опцию:
```
aggregate {
	nginx_upstream_check http://localhost/status;
}
```

Для проверки сертификатов X509 в файловой системе см. описание в [контексте x509](https://github.com/alligatormon/alligator/blob/master/doc/x509.md).
