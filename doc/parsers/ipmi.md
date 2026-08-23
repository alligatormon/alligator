**Language / Язык:** [English](ipmi.md) | [Русский](../ru/parsers/ipmi.md)

## IPMI

Runs `ipmitool` via `exec://` and parses sensor, SEL, chassis, LAN, and DCMI output.

### Connection URL

```
exec:///path/to/ipmitool
```

The binary must be ipmitool (or a compatible wrapper). Alligator invokes subcommands: `sensor`, `sel elist`, `chassis status`, `sel info`, `lan print`, `dcmi power reading`.

### Example

```
aggregate {
    ipmi exec:///usr/bin/ipmitool;
}
```

See [`src/tests/mock/ipmi/alligator.conf`](../../src/tests/mock/ipmi/alligator.conf).

### Requirements

- `ipmitool` installed and configured for local BMC access (IPMI device, LANplus credentials, etc.)
- Appropriate permissions to run sensor and SEL commands

### Metrics

Examples:

- `ipmi_sensor_{stat,status,thresholds}`
- `ipmi_eventlog_*`, `ipmi_lan`, `ipmi_dcmi_power_reading_*`
- Chassis and SEL summary counters
