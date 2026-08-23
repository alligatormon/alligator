**Language / Язык:** [English](../../parsers/named.md) | [Русский](named.md)

## Bind

```
statistics-channels {
    inet 127.0.0.1 port 8080 allow {any;};
};
```

### Сбор статистики
Bind должен быть запущен с указанным `statistics-channels`:
```
statistics-channels {
    inet 127.0.0.1 port 8080 allow {any;};
};
```

Дополнительно для каждой обслуживаемой зоны должна быть настроена опция `zone-statistics`.
```
zone "localhost" IN {
        type master;
        file "named.localhost";
        allow-update { none; };
        zone-statistics yes;
};
```

Чтобы включить сбор статистики с Bind, используйте следующую опцию:
```
aggregate {
	named http://localhost:8080;
}
```

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты:
```
system {
    process named;
    services isc-bind-named;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="named", src_port="53", proto="tcp"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="named", src_port="53", proto="udp"})';
	make socket_match;
	datasource internal;
}
```
