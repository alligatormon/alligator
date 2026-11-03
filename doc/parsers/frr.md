**Language / Язык:** [English](frr.md) | [Русский](../ru/parsers/frr.md)

## FRR (Free Range Routing)

Scrapes FRR daemon VTY Unix sockets the same way [frr_exporter](https://github.com/tynany/frr_exporter) does: `enable` then a JSON (or text) show command, NUL-framed. Metric names match the exporter (`frr_bgp_*`, `frr_bfd_*`, `frr_ospf_*`, `frr_route_*`, `frr_status_up`) so existing dashboards and alerts keep working.

Handler name is **`frr`**.

Alligator needs read/write access to the FRR sockets (typically group `frrvty`).

### Connection URL

```
unix:///var/run/frr
unix:///var/run/frr/zebra.vty
unix:///run/frr
```

If the path does not end with `.vty`, alligator appends `/zebra.vty` and treats the parent directory as the socket dir for the other daemons (`bgpd.vty`, `ospfd.vty`, `bfdd.vty`, …).

### Example

```
aggregate {
    frr unix:///var/run/frr period=30s timeout=20s;
}
```

Optional collectors (ProxySQL-style `env=` flags). Defaults match frr_exporter: BGP/BFD/OSPF/route on; BGP6 off.

```
aggregate {
    frr unix:///var/run/frr period=30s timeout=20s
        env=collect.bgp6:1
        env=collect.ospf:0;
}
```

| Flag | Default | Daemon / notes |
|------|---------|----------------|
| `collect.bgp` | on | `bgpd.vty` IPv4 unicast summary |
| `collect.bgp6` | off | `bgpd.vty` IPv6 unicast summary |
| `collect.bfd` | on | `bfdd.vty` peers |
| `collect.ospf` | on | `ospfd.vty` instance / interface / neighbor / max-age |
| `collect.route` | on | `zebra.vty` IPv4/IPv6 route summary |

`vtysh` is not used; only direct Unix sockets.

### Metrics (first slice)

- `frr_status_up{version,os}`
- `frr_collector_up{collector}`, `frr_scrape_duration_seconds{collector}`
- BGP: `frr_bgp_rib_count_total`, `frr_bgp_rib_memory_bytes`, `frr_bgp_peers_*`, `frr_bgp_peer_groups_*`, `frr_bgp_peer_message_{received,sent}_total`, `frr_bgp_peer_prefixes_*`, `frr_bgp_peer_state`, `frr_bgp_peer_uptime_seconds`
- BFD: `frr_bfd_peer_count`, `frr_bfd_peer_uptime`, `frr_bfd_peer_state`
- OSPF: `frr_ospf_neighbors`, `frr_ospf_neighbor_adjacencies`, LSA counters, `frr_ospf_neighbor_state`, `frr_ospf_data_ls_max_age`
- Route: `frr_route_total`, `frr_route_total_fib`

Unit tests: [`src/tests/unit2/parsers.h`](../../src/tests/unit2/parsers.h) (`api_test_parser_frr`), fixtures under `src/tests/mock/frr/`.
