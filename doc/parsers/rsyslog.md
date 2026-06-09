# rsyslog impstats

The `rsyslog-impstats` entrypoint handler parses [rsyslog impstats](https://www.rsyslog.com/doc/configuration/modules/impstats.html) messages forwarded over UDP or TCP.

## Configuration

```
entrypoint {
    handler rsyslog-impstats;
    udp 127.0.0.1:1111;
}
```

## rsyslog setup

Example `rsyslog.conf` fragment:

```
module(
    load="impstats"
    interval="10"
    resetCounters="off"
    log.file="off"
    log.syslog="on"
    ruleset="rs_impstats"
)

template(name="impformat" type="list") {
    property(outname="message" name="msg")
}

ruleset(name="rs_impstats" queue.type="LinkedList" queue.filename="qimp" queue.size="5000" queue.saveonshutdown="off") {
    *.* action (
        type="omfwd"
        target="127.0.0.1"
        port="1111"
        protocol="udp"
        Template="impformat"
    )
}
```

Each impstats message is parsed into one or more metric samples. The parser extracts the module name, origin, optional action, and per-key numeric values from the impstats header and body.

## Exported metrics

| Metric | Type | Description |
|--------|------|-------------|
| `rsyslog_stats` | gauge | Numeric impstats counter or rate for a module/origin/key combination. |

### Labels

| Label | Description |
|-------|-------------|
| `module` | rsyslog module name from the impstats header. |
| `origin` | Origin field from the impstats header (`origin=...`). |
| `action` | Action name when present in the header; omitted otherwise. |
| `key` | Statistic key from the impstats body. |

### Example output

```
# TYPE rsyslog_stats gauge
# HELP rsyslog_stats Rsyslog impstats value by module, origin, action, and key.
rsyslog_stats{module="core.action",origin="core.action",key="processed"} 42
```
