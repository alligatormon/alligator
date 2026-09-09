**Language / Язык:** [English](freeradius.md) | [Русский](../ru/parsers/freeradius.md)

## FreeRADIUS

Collects FreeRADIUS authentication, accounting, proxy, and queue statistics.

Handler name is **`freeradius`**. Two input forms:

1. **RADIUS Status-Server** over UDP (same approach as Telegraf `inputs.freeradius`). The URL user or password is the shared secret. Default status port is **18121**.
2. **Text** from `radmin stats` or `radclient` attribute dumps (`exec://`). `mesg` is empty; stdout is parsed.

Enable `status_server = yes` (and a status client with a secret) in `radiusd.conf` for the UDP path.

### Connection URL

```
udp://adminsecret@127.0.0.1:18121
udp://:adminsecret@127.0.0.1:18121
exec://radmin
```

### Example

Status-Server (secret as URL user, SNMP-community style):

```
aggregate {
    freeradius udp://adminsecret@127.0.0.1:18121;
}
```

Via `radmin`:

```
aggregate {
    freeradius 'exec://radmin -q stats';
}
```

Non-default control socket:

```
aggregate {
    freeradius 'exec://radmin -f /var/run/radiusd/radiusd.sock -q stats';
}
```

FreeRADIUS 3.2+ may also expose Prometheus at an HTTP port; scrape that with [`prometheus_metrics`](prometheus_metrics.md).

### Metrics

Counters (examples): `freeradius_access_requests_total`, `freeradius_access_accepts_total`, `freeradius_access_rejects_total`, `freeradius_accounting_requests_total`, proxy variants.

Gauges: `freeradius_queue_len_{internal,proxy,auth,acct,detail}`, `freeradius_start_time_seconds`, `freeradius_hup_time_seconds`.

Unit tests: [`src/tests/unit2/parsers.h`](../../src/tests/unit2/parsers.h) (`api_test_parser_freeradius`).
