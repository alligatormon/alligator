## nats.io

To enable the collection of statistics from NATS, use the following option:
```
aggregate {
    nats http://localhost:8222;
}
```

NATS must be started with the monitoring port (`-m 8222`). Alligator scrapes `/varz`, `/connz`, `/routez`, and `/subsz`.

### Metrics (selective parser)

The parser does **not** dump every JSON field. That used to create useless series such as `nats_connz_connections_tls_version{cid="…"} 1` (string fields coerced to gauge `1`) and high-cardinality per-connection metrics.

| Endpoint | What is exported |
|----------|------------------|
| `/varz` | Numeric/bool fields (`nats_varz_*`). Identity strings (`version`, `server_id`, …) as `nats_varz_<key>{value="…"} 1`. `http_req_stats` as `nats_varz_http_req_stats{endpoint="/varz"}`. |
| `/subsz` | Numeric fields as `nats_subz_*` |
| `/connz` | **Aggregates only** (like prometheus-nats-exporter `-connz`): `num_connections`, `total`, `limit`, summed `in_msgs`/`out_bytes`/… — **no** per-`cid` series |
| `/routez` | `num_routes` plus per-route numerics labeled by `rid` |

It is also useful to check process statistics, running services and open ports:
```
system {
    process nats-server;
    services nats-server.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="nats-server", src_port="4222"})';
	make socket_match;
	datasource internal;
}
```
