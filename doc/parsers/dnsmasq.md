**Language / Язык:** [English](dnsmasq.md) | [Русский](../ru/parsers/dnsmasq.md)

## dnsmasq

Collects dnsmasq cache statistics over DNS (CHAOS class TXT) and optional DHCP lease counts from the leases file.

Handler names:

- **`dnsmasq`** — CHAOS TXT queries (`cachesize.bind`, `insertions.bind`, `evictions.bind`, `misses.bind`, `hits.bind`, `auth.bind`, `servers.bind`)
- **`dnsmasq_dhcp`** — parse a dnsmasq leases file (`file://`)

dnsmasq must answer CHAOS queries (default). The aggregator sends one UDP query per statistic.

### Connection URL

```
udp://127.0.0.1:53
file:///var/lib/misc/dnsmasq.leases
```

### Example

```
aggregate {
    dnsmasq udp://127.0.0.1:53;
    dnsmasq_dhcp file:///var/lib/misc/dnsmasq.leases;
}
```

### Metrics

CHAOS (`dnsmasq`):

- `dnsmasq_cachesize` — configured cache size
- `dnsmasq_insertions_total`, `dnsmasq_evictions_total`, `dnsmasq_misses_total`, `dnsmasq_hits_total`, `dnsmasq_auth_total`
- `dnsmasq_servers_queries_total{server}`, `dnsmasq_servers_queries_failed_total{server}` — per upstream (`addr#port`)

DHCP leases (`dnsmasq_dhcp`):

- `dnsmasq_dhcp_leases` — lease file entries
- `dnsmasq_dhcp_leases_active` — entries whose expiry is in the future

Unit tests: [`src/tests/unit2/parsers.h`](../../src/tests/unit2/parsers.h) (`api_test_parser_dnsmasq`, `api_test_parser_dnsmasq_dhcp`).
