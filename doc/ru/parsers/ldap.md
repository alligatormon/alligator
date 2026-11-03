**Language / Язык:** [English](../../parsers/ldap.md) | [Русский](ldap.md)

## LDAP / OpenLDAP

Минимальный LDAP-клиент для сбора метрик OpenLDAP из `cn=Monitor` (как у классического openldap_exporter). Имя handler — **`ldap`**.

Каждый scrape отправляет BindRequest и один SearchRequest (subtree) под `cn=Monitor` для:

- `objectClass=monitorCounterObject` → `monitorCounter`
- `objectClass=monitoredObject` → `monitoredInfo`
- `objectClass=monitorOperation` → `monitorOpCompleted`

TLS — обычный клиентский TLS alligator: `tls://` для LDAPS (обычно порт 636). StartTLS на 389 не реализован.

### Безопасность

- Для bind с паролем предпочтительны `tls://` (LDAPS) или `unix://` (ldapi); по `tcp://` учётные данные уходят открытым текстом.
- Для `tls://`, если `tls_verify` не задан и есть `tls_ca` или пароль bind, alligator включает `tls_verify=on` (как у action HTTPS). `tls_verify=off` — только при осознанном отключении проверки.
- Anonymous bind (без user/pass в URL) по `tcp://` разрешён.

### Connection URL

```
tcp://127.0.0.1:389
tls://cn=Manager,dc=example,dc=com:secret@ldap.example:636 tls_ca=/etc/ssl/certs/ca.pem
tls://cn=monitoring,cn=Monitor:secret@ldap.example:636 tls_ca=/etc/ssl/certs/ca.pem tls_verify=on
unix:///var/run/slapd/ldapi
tcp://user:pass@127.0.0.1:389
tcp://127.0.0.1:389/cn=Monitor
```

- `user` / `pass` в URL — bind DN и пароль. Без них — anonymous bind.
- Непустой path (query для не-HTTP) задаёт search base (по умолчанию `cn=Monitor`).
- Запятые в DN допустимы как есть; экранировать нужно только `:`, `@` и `/` в DN или пароле.

У OpenLDAP bind DN должен иметь право читать monitor database (см. [monitor backend](https://www.openldap.org/doc/admin24/monitoringslapd.html)).

Если в логе `bind_ok=1 search_ok=0` и `openldap_search_result_code` **50** (`insufficientAccessRights`) или **32** (`noSuchObject`) — bind прошёл, но `cn=Monitor` закрыт или отсутствует. Выдайте read scrape DN, например:

```
# ldapmodify -Y EXTERNAL -H ldapi:// <<EOF
dn: olcDatabase={1}monitor,cn=config
changetype: modify
replace: olcAccess
olcAccess: to * by dn.exact="uid=test,ou=users,dc=corpmail,dc=ru" read by * none
-
EOF
```

Проверка:

```
ldapsearch -x -H ldap://127.0.0.1:389 \
  -D 'uid=test,ou=users,dc=corpmail,dc=ru' -w '...' \
  -b 'cn=Monitor' '(objectClass=monitorCounterObject)' monitorCounter
```

### Пример

```
entrypoint {
    tcp 1111;
}

aggregate {
    ldap tls://cn=monitoring,cn=Monitor:secret@ldap.example:636 tls_ca=/etc/ssl/certs/ca.pem;
    ldap unix:///var/run/slapd/ldapi;
    ldap tcp://127.0.0.1:389;
}
```

Метрики снимайте по **HTTP** (plain `tcp` entrypoint — не TLS):

```
curl -sS http://127.0.0.1:1111/metrics | grep openldap_
```

`https://` — только если entrypoint `tls <port>` (тогда обычно `curl -k https://127.0.0.1:1111/metrics`). После scrape alligator молча ждёт следующий period — это нормально.

JSON:

```json
{
  "entrypoint": [{"tcp": ["1111"]}],
  "aggregate": [{
    "handler": "ldap",
    "url": "tls://cn=monitoring,cn=Monitor:secret@ldap.example:636",
    "tls_ca": "/etc/ssl/certs/ca.pem"
  }]
}
```

### Метрики

| Metric | Labels | Source |
|--------|--------|--------|
| `openldap_up` | — | 1 если bind **и** search вернули LDAP success |
| `openldap_monitor_counter_object` | `dn` | `monitorCounter` |
| `openldap_monitored_object` | `dn` | `monitoredInfo` (только числовые) |
| `openldap_monitor_operation` | `dn` | `monitorOpCompleted` |

Имена как у Go openldap_exporter (`openldap_monitor_operation`); старый Python exporter использовал `openldap_monitored_op` для тех же значений.

Replication `contextCSN` пока не собирается.

Unit tests: [`src/tests/unit2/parsers.h`](../../../src/tests/unit2/parsers.h) (`api_test_parser_ldap`).
