## SNMP

To poll SNMP agents from Alligator, add `snmp` targets under `aggregate`. The implementation uses **SNMPv2c** (community string authentication). The community is taken from the URL user part; the host and port identify the agent; the **query path** selects the OID and request type (GET, GET-NEXT, or walk).

```
aggregate {
    # Single scalar (GET), e.g. sysUpTime.0
    snmp udp://public@192.0.2.1:161/1.3.6.1.2.1.1.3.0;

    # Walk a subtree (GET-NEXT chain); cap is compile-time (default 100000 steps)
    snmp udp://public@192.0.2.1:161/walk/1.3.6.1.2.1.2.2.1;

    # Optional GET-NEXT without walk prefix
    snmp udp://public@192.0.2.1:161/next/1.3.6.1.2.1.1.1.0;

    # Label series with a fixed mib= tag; shorten oid label to suffix under a prefix
    snmp udp://public@192.0.2.1:161/walk/1.3.6.1.2.1.25.3.2.1
        'env=snmp_mib:HOST-RESOURCES-MIB'
        'env=snmp_oid_strip_prefix:1.3.6.1.2.1.25'
        'env=snmp_friendly_names:1';

    # Load vendor MIBs for OID→symbol on OBJECT IDENTIFIER values (see below)
    snmp udp://public@192.0.2.1:161/1.3.6.1.2.1.1.2.0
        'env=snmp_mib_dirs:/usr/share/snmp/mibs:/opt/mibs';
}
```

Per-target options are passed as `env=key:value` on the same line (see `doc/aggregate.md`).

### URL path

- **`<dotted-oid>`** — SNMP GET for that OID.
- **`walk/<dotted-oid>`** — Repeated GET-NEXT starting after the subtree root until the subtree ends or the walk step limit is reached.
- **`next/...`** or **`getnext/...`** — Single GET-NEXT request (same walk machinery without looping).

The path must contain a non-empty OID except for `walk/`, where the subtree OID follows `walk/` (leading/trailing slashes are ignored).

### Environment variables (scrape context)

| Key | Role |
|-----|------|
| `snmp_mib` | If set, every emitted metric gets an extra label `mib=<value>` (literal string, not parsed from a file). |
| `snmp_oid_strip_prefix` | Dotted OID prefix to strip from the `oid` label; remainder is shown (handy with `snmp_mib` for readability). |
| `snmp_friendly_names` | When `1` / `true` / `on` / `yes`, known OIDs map to stable metric names such as `snmp_mib2_sys_up_time` or table-specific names (`snmp_hr_*`, TCP/UDP helpers, etc.) instead of the generic `snmp_scrape_*` names. |
| `snmp_metric_from_oid` | When enabled, metric names are derived from the OID (with suffixes like `_string` / `_missing` where applicable) instead of `snmp_scrape_*`. |
| `snmp_decode_tcp` | When `1` / `true` / `on` / `yes`, TCP (and related) table indices are decoded into labels (`local_addr`, `local_port`, `rem_addr`, `rem_port`, `state` for connection state). |
| `snmp_decode_tcp_metric_names` | Default on; set to `0` / `false` / `off` / `no` to keep numeric scrape metrics for TCP tables even when `snmp_decode_tcp` is on. |
| `snmp_mib_dirs` | Colon-separated list of directories with `*.mib` / `*.txt` definitions. Used to resolve **MODULE-IDENTITY** symbols for OID→name and for `symbol` labels on OBJECT IDENTIFIER octet strings. Reloaded when this value changes (see `snmp_debug`). |
| `snmp_oid_symbols` | Default on; set to `0` / `false` / `off` / `no` to disable resolving dotted-OID strings to well-known module symbols. |
| `snmp_debug` | `0` / unset — no extra logs. `1` / `on` — MIB reload and miss handling. `2` / `verbose` / `all` — verbose symbol logging. Requires debug log level on the target to see `carglog` output. |

### Metrics and labels

- Default numeric series use **`snmp_scrape_value`** with label **`oid`** (full dotted OID or suffix if `snmp_oid_strip_prefix` is set). **`snmp_scrape_missing`** records `noSuchObject` / `noSuchInstance` with `reason`.
- String / octet values use **`snmp_scrape_string`** with **`oid`** and **`value`** (and optional **`symbol`** when the value is a dotted OID and symbol resolution is enabled). Some OIDs get special formatting (e.g. `hrSystemDate`).
- With **`snmp_friendly_names`**, table rows may add **`hr_index`**, **`index`**, or decoded TCP/UDP address labels as documented in code (`snmp_friendly_metric_name_by_oid` and TCP decode paths).
- Agent **`error_status`** is exposed as **`snmp_error`** with labels `status=error_status`, `index=0`.
- **`snmp_mib`** adds a **`mib`** label to all metrics from that scrape when set.

### MIB files

Optional MIB directories (`snmp_mib_dirs`) are scanned for object definitions; the reverse map prefers **MODULE-IDENTITY** OIDs so symbol names stay unambiguous. This complements the built-in OID→name tables used for friendly metric names and TCP/UDP decoding.

### Limitations

- **SNMPv3** is not implemented; only community-based v2c over UDP as built by `snmp_mesg`.
- Walk depth is bounded by **`SNMP_WALK_MAX_STEPS`** (default 100000 unless overridden at build time).
- Request/response size is capped (e.g. **2048** bytes for the built packet); very large varbinds may not be handled.
