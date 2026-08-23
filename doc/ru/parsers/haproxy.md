**Language / Язык:** [English](../../parsers/haproxy.md) | [Русский](haproxy.md)

## HAproxy

Чтобы включить сбор статистики с HAproxy, используйте следующую опцию:
### через TCP-сокет
```
aggregate {
    haproxy tcp://localhost:9999;
}
```

### через unix-сокет
```
aggregate {
    haproxy unix:///var/run/haproxy;
}
```
