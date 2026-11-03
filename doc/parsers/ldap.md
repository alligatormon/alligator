**Language / Язык:** [English](ldap.md) | [Русский](../ru/parsers/ldap.md)

## LDAP / OpenLDAP

Minimal LDAP client that scrapes OpenLDAP `cn=Monitor` (same metrics as the classic openldap_exporter). Handler name is **`ldap`**.

Each scrape pipelines a BindRequest and one subtree SearchRequest under `cn=Monitor` for:

- `objectClass=monitorCounterObject` → `monitorCounter`
- `objectClass=monitoredObject` → `monitoredInfo`
- `objectClass=monitorOperation` → `monitorOpCompleted`

TLS is ordinary alligator client TLS: use `tls://` for LDAPS (typically port 636). StartTLS on port 389 is not implemented.

### Security notes

- **Password binds over `tcp://` are refused** unless you set `env=ldap_allow_cleartext_bind:1` (local/lab only). Prefer `tls://` or `unix://` (ldapi).
- For `tls://`, if `tls_verify` is unset and either `tls_ca` or a bind password is present, alligator enables `tls_verify=on` automatically (same idea as action HTTPS). Set `tls_verify=off` only when you intentionally skip peer checks.
- Anonymous bind (no URL user/pass) over `tcp://` is still allowed.

### Connection URL

```
tcp://127.0.0.1:389
tls://cn=Manager,dc=example,dc=com:secret@ldap.example:636 tls_ca=/etc/ssl/certs/ca.pem
tls://cn=monitoring,cn=Monitor:secret@ldap.example:636 tls_ca=/etc/ssl/certs/ca.pem tls_verify=on
unix:///var/run/slapd/ldapi
tcp://user:pass@127.0.0.1:389 env=ldap_allow_cleartext_bind:1
tcp://127.0.0.1:389/cn=Monitor
```

- URL `user` / `pass` are the bind DN and password. Omit both for anonymous bind.
- A non-empty path (non-HTTP query) overrides the search base (default `cn=Monitor`).
- Prefer commas in the bind DN as-is; only `:`, `@`, and `/` in the DN or password need URL escaping.

OpenLDAP must allow the bind DN to read the monitor database (see the [OpenLDAP monitor backend](https://www.openldap.org/doc/admin24/monitoringslapd.html)).

If alligator logs `bind_ok=1 search_ok=0` with `openldap_search_result_code` **50** (`insufficientAccessRights`) or **32** (`noSuchObject`), the bind works but `cn=Monitor` is denied or missing. Grant read access to the scrape DN, for example:

```
# ldapmodify -Y EXTERNAL -H ldapi:// <<EOF
dn: olcDatabase={1}monitor,cn=config
changetype: modify
replace: olcAccess
olcAccess: to * by dn.exact="uid=test,ou=users,dc=corpmail,dc=ru" read by * none
-
EOF
```

Confirm with:

```
ldapsearch -x -H ldap://127.0.0.1:389 \
  -D 'uid=test,ou=users,dc=corpmail,dc=ru' -w '...' \
  -b 'cn=Monitor' '(objectClass=monitorCounterObject)' monitorCounter
```

### Example

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

Verify metrics over **HTTP** (plain `tcp` entrypoint is not TLS):

```
curl -sS http://127.0.0.1:1111/metrics | grep openldap_
```

Use `https://` only when the entrypoint is `tls <port>` (then typically `curl -k https://127.0.0.1:1111/metrics`). After a scrape, alligator stays running and waits for the next period — silence on stdout is normal.

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

### Metrics

| Metric | Labels | Source |
|--------|--------|--------|
| `openldap_up` | — | 1 if bind **and** search both return LDAP success |
| `openldap_monitor_counter_object` | `dn` | `monitorCounter` |
| `openldap_monitored_object` | `dn` | `monitoredInfo` (numeric only) |
| `openldap_monitor_operation` | `dn` | `monitorOpCompleted` |

Metric names match the Go openldap_exporter (`openldap_monitor_operation`); the older Python exporter used `openldap_monitored_op` for the same values.

Replication `contextCSN` gauges are not scraped yet.

Unit tests: [`src/tests/unit2/parsers.h`](../../src/tests/unit2/parsers.h) (`api_test_parser_ldap`).
