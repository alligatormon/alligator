# auditd

The `auditd` entrypoint handler parses Linux audit log records in the native `key=value` format and exports them as Prometheus metrics.

Each non-empty input line increments the `auditd_event` counter by one. Selected audit fields become metric labels.

This handler counts **audit log events**. It does not parse auditd daemon status from `auditctl -s` or internal queue statistics.

## Configuration

```
entrypoint {
    handler auditd;
    tcp 127.0.0.1:1514;
}
```

UDP is also supported:

```
entrypoint {
    handler auditd;
    udp 127.0.0.1:1514;
}
```

The handler expects the message body to contain one or more audit records separated by newlines. Empty lines are ignored.

## Input format

A typical audit record looks like this:

```
type=SYSCALL msg=audit(1710000000.123:456): arch=c000003e syscall=59 success=yes exit=0 auid=1000 uid=1000 gid=1000 exe="/usr/bin/bash" comm="bash" key="my-rule"
```

Parsing rules:

- Fields are parsed as `key=value` pairs.
- Double-quoted and single-quoted values may contain spaces.
- Tokens without `=` (for example a syslog prefix) are skipped.
- Only a subset of audit fields is exported as labels (see below).

## Exported metrics

| Metric | Type | Description |
|--------|------|-------------|
| `auditd_event` | counter | Number of received audit log records. |

### Labels

| Label | Source audit fields | Notes |
|-------|---------------------|-------|
| `type` | `type` | Event type, for example `SYSCALL`, `EXECVE`, `USER_AUTH`. |
| `success` | `success` | Usually `yes` or `no`. |
| `exe` | `exe` | Executable path. Quoted values with spaces are supported. |
| `key` | `key` | Audit rule key, for example `my-rule` or `(null)`. |
| `AUID` | `auid` or `AUID` | Stored as `AUID`. |
| `UID` | `uid` or `UID` | Stored as `UID`. |
| `GID` | `gid` or `GID` | Stored as `GID`. |

Example exposition:

```
# HELP auditd_event Number of audit log events received, labeled by event attributes.
# TYPE auditd_event counter
auditd_event{type="SYSCALL",success="yes",exe="/usr/bin/bash",key="my-rule",AUID="1000",UID="1000",GID="1000"} 42
```

Use `rate()` or `increase()` on this counter to measure event rates per label combination.

## Forwarding audit logs with rsyslog

A common setup is to forward `/var/log/audit/audit.log` lines to Alligator over TCP or UDP.

rsyslog example:

```
module(load="imfile" PollingInterval="10")

input(type="imfile"
      File="/var/log/audit/audit.log"
      Tag="audit:"
      Severity="info"
      Facility="local6")

action(type="omfwd"
       Target="127.0.0.1"
       Port="1514"
       Protocol="tcp"
       Template="RSYSLOG_TraditionalForwardFormat")
```

Configure the Alligator entrypoint on `127.0.0.1:1514` with `handler auditd`.

If the forwarded message includes a syslog header, the parser skips non `key=value` tokens and still extracts audit fields from the same line.

## Limitations

- One metric per audit line; related multi-line records (`PATH`, `CWD`, `PROCTITLE`) are counted separately.
- Most audit fields (`syscall`, `pid`, `comm`, `arch`, and others) are not exported as labels.
- Label values longer than 255 characters are truncated.
