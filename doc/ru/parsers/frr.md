**Language / Язык:** [English](../../parsers/frr.md) | [Русский](frr.md)

## FRR (Free Range Routing)

Собирает метрики с Unix-сокетов VTY демонов FRR так же, как [frr_exporter](https://github.com/tynany/frr_exporter): `enable`, затем show-команда, кадры с NUL. Имена метрик совпадают с экспортёром (`frr_bgp_*`, `frr_bfd_*`, `frr_ospf_*`, `frr_route_*`, `frr_status_up`).

Имя хендлера: **`frr`**.

Нужен доступ на чтение/запись к сокетам FRR (обычно группа `frrvty`).

### URL подключения

```
unix:///var/run/frr
unix:///var/run/frr/zebra.vty
unix:///run/frr
```

Если путь не заканчивается на `.vty`, alligator добавляет `/zebra.vty` и берёт родительский каталог как каталог сокетов остальных демонов.

### Пример

```
aggregate {
    frr unix:///var/run/frr period=30s timeout=20s;
}
```

Опциональные коллекторы через `env=` (как у ProxySQL). По умолчанию как у frr_exporter: BGP/BFD/OSPF/route включены; BGP6 выключен.

```
aggregate {
    frr unix:///var/run/frr period=30s timeout=20s
        env=collect.bgp6:1
        env=collect.ospf:0;
}
```

| Флаг | По умолчанию | Демон |
|------|--------------|--------|
| `collect.bgp` | вкл | `bgpd.vty` IPv4 unicast |
| `collect.bgp6` | выкл | `bgpd.vty` IPv6 unicast |
| `collect.bfd` | вкл | `bfdd.vty` |
| `collect.ospf` | вкл | `ospfd.vty` |
| `collect.route` | вкл | `zebra.vty` summary |

`vtysh` не используется.

### Метрики

См. английскую документацию: [frr.md](../../parsers/frr.md).

Юнит-тесты: `api_test_parser_frr`, фикстуры в `src/tests/mock/frr/`.
