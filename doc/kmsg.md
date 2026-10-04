# Kernel log (`/dev/kmsg`)

`kmsg://` is a log source for `grok`, `mtail`, and `vrl`. It reads the Linux kernel ring at `/dev/kmsg` and feeds each record to the same parser path as `file://` and `kafka://`.

`file:///dev/kmsg` is not this source. The file tailer uses file size and `pread` offsets. `/dev/kmsg` is a character device: each `read()` returns one record, and `lseek(fd, 0, SEEK_END)` means "after the last record", not a byte offset.

## Source URL

Plain config and the JSON API use the same URL.

```
aggregate {
    mtail kmsg:// name=kernel;
    grok kmsg:// name=kernel_grok;
    vrl kmsg:// name=kernel_vrl;
}
```

```json
{
  "aggregate": [
    {"handler": "mtail", "url": "kmsg://", "name": "kernel"},
    {"handler": "grok", "url": "kmsg://", "name": "kernel_grok"},
    {"handler": "vrl", "url": "kmsg://", "name": "kernel_vrl"}
  ]
}
```

`kmsg://` opens `/dev/kmsg`. `kmsg:///dev/kmsg` is the same device written out in full. Any other path (`kmsg:///path`) is opened as given.

On open, Alligator seeks to the end of the ring. Messages already in the buffer are not replayed, and a restart does not catch up lines printed while the process was down. There is no byte offset to save.

The reader is non-blocking and waits on the process event loop. A short read is buffered until the record is complete. `EPIPE` means the ring overwrote unread records; reading continues at the next record. If the descriptor fails, it is reopened after one second (not in a tight loop) and seeked to the end again.

## Line format

Parsers see one text line per record. The kernel header is removed.

A raw record looks like:

```
6,339,5140900,-;BUG: soft lockup - CPU#0 stuck for 22s! [kworker/0:1:9]
 SUBSYSTEM=watchdog
```

The line passed to mtail, grok, and vrl is:

```
BUG: soft lockup - CPU#0 stuck for 22s! [kworker/0:1:9]
```

- `<priority>,<sequence>,<timestamp>,<flag>` and an optional `,caller=` field are stripped.
- `\xHH` escapes are turned back into bytes.
- Dictionary lines that follow the message and start with a space (`SUBSYSTEM=`, `DEVICE=`) are dropped.
- A continuation fragment (`+` or `c` in the flag) is still its own line. Lockup and allocation messages are complete records (`-`).

Match the message text. Do not match the numeric prefix.

## mtail: lockups, hung tasks, allocation failures

`/etc/alligator/mtail/kernel.mtail`:

```
counter kernel_soft_lockup_total
counter kernel_hard_lockup_total
counter kernel_hung_task_total
counter kernel_page_alloc_failure_total

/BUG: soft lockup/ {
    kernel_soft_lockup_total++
}

/Watchdog detected hard LOCKUP|hard lockup/ {
    kernel_hard_lockup_total++
}

/INFO: task .* blocked for more than|hung task/ {
    kernel_hung_task_total++
}

/page allocation failure/ {
    kernel_page_alloc_failure_total++
}
```

```
mtail {
    name kernel;
    script /etc/alligator/mtail/kernel.mtail;
}

aggregate {
    mtail kmsg:// name=kernel;
}
```

The four names are counters. A Prometheus rate over a scrape interval:

```
rate(kernel_soft_lockup_total[5m])
rate(kernel_hard_lockup_total[5m])
rate(kernel_hung_task_total[5m])
rate(kernel_page_alloc_failure_total[5m])
```

| Counter | Kernel text |
|---------|-------------|
| `kernel_soft_lockup_total` | `BUG: soft lockup` |
| `kernel_hard_lockup_total` | `Watchdog detected hard LOCKUP` or `hard lockup` |
| `kernel_hung_task_total` | `INFO: task … blocked for more than` or `hung task` |
| `kernel_page_alloc_failure_total` | `page allocation failure` |

## Permission and when lines show up

The process needs read permission on `/dev/kmsg`. That node is often mode `0640` and owned by `root` with group `adm` (or `systemd-journal`). Without that access the open fails and is retried once a second.

A counter moves only when the kernel prints the line:

- hung tasks: `kernel.hung_task_timeout_secs` (0 disables the message)
- soft lockups: watchdog threshold (`kernel.watchdog_thresh`)
- hard lockups: NMI watchdog
- page allocation failures: the kernel is actually failing an allocation

`log_channel_raw` works on `kmsg://` the same way as on `file://`: the decoded line is forwarded, not the raw record.
