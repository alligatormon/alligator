**Language / Язык:** [English](../../parsers/dnsmasq.md) | [Русский](dnsmasq.md)

## dnsmasq

Собирает статистику кэша dnsmasq по DNS (CHAOS TXT) и опционально число DHCP-аренда из файла leases.

Имена handler:

- **`dnsmasq`** — CHAOS TXT (`cachesize.bind`, `insertions.bind`, `evictions.bind`, `misses.bind`, `hits.bind`, `auth.bind`, `servers.bind`)
- **`dnsmasq_dhcp`** — разбор файла leases dnsmasq (`file://`)

dnsmasq должен отвечать на CHAOS-запросы (по умолчанию так и есть). Агрегатор отправляет один UDP-запрос на каждый счётчик.

### Connection URL

```
udp://127.0.0.1:53
file:///var/lib/misc/dnsmasq.leases
```

### Пример

```
aggregate {
    dnsmasq udp://127.0.0.1:53;
    dnsmasq_dhcp file:///var/lib/misc/dnsmasq.leases;
}
```

### Metrics

CHAOS (`dnsmasq`):

- `dnsmasq_cachesize` — размер кэша
- `dnsmasq_insertions`, `dnsmasq_evictions`, `dnsmasq_misses`, `dnsmasq_hits`, `dnsmasq_auth`
- `dnsmasq_servers_queries{server}`, `dnsmasq_servers_queries_failed{server}` — по upstream (`addr#port`)

DHCP leases (`dnsmasq_dhcp`):

- `dnsmasq_dhcp_leases` — записи в файле leases
- `dnsmasq_dhcp_leases_active` — записи с expiry в будущем

Unit tests: [`src/tests/unit2/parsers.h`](../../../src/tests/unit2/parsers.h) (`api_test_parser_dnsmasq`, `api_test_parser_dnsmasq_dhcp`).
