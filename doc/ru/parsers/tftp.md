**Language / Язык:** [English](../../parsers/tftp.md) | [Русский](tftp.md)

## tftp

Чтобы включить сбор статистики с tftp, используйте следующую опцию:
```
aggregate {
    tftp udp://localhost:69/ping;
}
```
Это проверит доступность файла `ping`.

Также полезно проверять статистику процесса, запущенные сервисы и открытые порты. TFTP обычно используется со службой inetd/xinetd:
```
system {
    process xinetd;
    services xinetd.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="xinetd", src_port="69"})';
	make socket_match;
	datasource internal;
}
```
