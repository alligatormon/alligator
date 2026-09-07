**Language / Язык:** [English](nginx.md) | [Русский](../ru/parsers/nginx.md)

# Nginx

Open-source Nginx exposes the classic **stub_status** page. Alligator scrapes it with handler **`nginx`**.

Enable it in an `http` / `server` context:

```
location /nginx_status {
    stub_status;
    allow 127.0.0.1;
    deny all;
}
```

Collect:

```
aggregate {
    nginx http://127.0.0.1/nginx_status;
}
```

### Metrics

- `nginx_connections{state="active|reading|writing|waiting"}`
- `nginx_accepts_total`, `nginx_handled_total`, `nginx_requests_total`

Handler name is **`nginx`** (source `src/parsers/nginx.c`). Unit tests: [`src/tests/unit2/parsers.h`](../../src/tests/unit2/parsers.h) (`api_test_parser_nginx_stub_status`).

It is still useful to watch the process, systemd unit, and listening ports:

```
system {
    process nginx;
    services nginx.service;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="nginx", src_port="80"})';
	make socket_match;
	datasource internal;
}

query {
	expr 'count by (src_port, process) (socket_stat{process="nginx", src_port="443"})';
	make socket_match;
	datasource internal;
}
```

The command `nginx -t` returns the validation status of the configuration:

```
aggregate {
	process 'exec:///sbin/nginx -t';
}
```

This produces:

```
alligator_process_exit_status {proto="shell", key="exec:process:/sbin/nginx -t:/", type="aggregator"} 0
alligator_process_term_signal {proto="shell", key="exec:process:/sbin/nginx -t:/", type="aggregator"} 0
```

## nginx upstream check module

Alligator also supports collecting metrics from the [upstream check module](https://github.com/yaoweibin/nginx_upstream_check_module), which is widely used for active health checks.

The following configuration should be specified within a server context to enable metric transfer:

```
location /status {
    check_status;
}
```

To enable statistics collection from the upstream check module, use the following option:

```
aggregate {
	nginx_upstream_check http://localhost/status;
}
```

Handler **`nginx_upstream_check`** is separate from **`nginx`** (stub_status).

For checking X509 certificates on the filesystem, please refer to the explanation provided in the x509 [context](https://github.com/alligatormon/alligator/blob/master/doc/x509.md).
