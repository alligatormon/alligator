**Language / Язык:** [English](fail2ban.md) | [Русский](../ru/parsers/fail2ban.md)

## fail2ban

Parses stdout of `fail2ban-client status` (overview) and `fail2ban-client status <jail>` (per-jail counters).

Handler name is **`fail2ban`**. `mesg` is empty: the aggregator runs the `exec://` command and passes stdout to the parser. Quote the URL when the command has arguments.

### Connection URL

```
exec://fail2ban-client
```

### Example

Overview (jail count):

```
aggregate {
    fail2ban 'exec://fail2ban-client status';
}
```

One jail:

```
aggregate {
    fail2ban 'exec://fail2ban-client status sshd';
}
```

Non-default server socket:

```
aggregate {
    fail2ban 'exec://fail2ban-client --socket /run/fail2ban/fail2ban.sock status sshd';
}
```

### Metrics

- `fail2ban_jails` — from the global `Status` overview (`Number of jail`)
- `fail2ban_failed{jail}` — currently failed attempts
- `fail2ban_banned{jail}` — currently banned addresses

Unit tests: [`src/tests/unit2/parsers.h`](../../src/tests/unit2/parsers.h) (`api_test_parser_fail2ban`).
