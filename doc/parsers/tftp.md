**Language / Язык:** [English](tftp.md) | [Русский](../ru/parsers/tftp.md)

## TFTP

Active TFTP check: requests a file over UDP and records availability / timing.

### Connection URL

```
udp://host[:69]/filename
```

Default TFTP port is **69**. The path is the remote filename to fetch (for example `/ping`).

### Example

```
aggregate {
    tftp udp://localhost:69/ping;
}
```

This checks availability of the file `ping`.

### Metrics

Blackbox-style connectivity metrics for the TFTP transfer (success / timing).

Optional process / socket checks (TFTP is often behind inetd/xinetd):

```
system {
    process xinetd;
    services xinetd.service;
}

query {
    expr 'count by (src_port, process) (socket_stat{process="xinetd", src_port="69"})';
    make socket_match;
    datasource internal;
}
```
