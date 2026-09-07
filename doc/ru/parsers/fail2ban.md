**Language / Язык:** [English](../../parsers/fail2ban.md) | [Русский](fail2ban.md)

## fail2ban

Разбирает stdout команд `fail2ban-client status` (обзор) и `fail2ban-client status <jail>` (счётчики jail).

Имя handler — **`fail2ban`**. `mesg` пустой: агрегатор запускает `exec://` и передаёт stdout в parser. URL с аргументами заключайте в кавычки.

### Connection URL

```
exec://fail2ban-client
```

### Пример

Обзор (число jail):

```
aggregate {
    fail2ban 'exec://fail2ban-client status';
}
```

Один jail:

```
aggregate {
    fail2ban 'exec://fail2ban-client status sshd';
}
```

Нестандартный socket сервера:

```
aggregate {
    fail2ban 'exec://fail2ban-client --socket /run/fail2ban/fail2ban.sock status sshd';
}
```

### Metrics

- `fail2ban_jails` — из глобального обзора `Status` (`Number of jail`)
- `fail2ban_failed{jail}` — текущие failed attempts
- `fail2ban_banned{jail}` — текущие banned addresses

Unit tests: [`src/tests/unit2/parsers.h`](../../../src/tests/unit2/parsers.h) (`api_test_parser_fail2ban`).
