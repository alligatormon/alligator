# Mtail Examples

This page contains practical examples for using mtail in Alligator.

## 1) Basic Line Counter

Mtail script (`/etc/alligator/mtail/linecount.mtail`):

```
counter lines_total

/$/ {
  lines_total++
}
```

Alligator config:

```
mtail {
    name linecount;
    script /etc/alligator/mtail/linecount.mtail;
}

entrypoint {
    bind 0.0.0.0:19101;
    handler mtail;
    mtail linecount;
}
```

Every ingested line increments `lines_total`.

## 2) Histogram by HTTP Code

Mtail script (`/etc/alligator/mtail/latency.mtail`):

```
histogram webserver_latency_by_code by code buckets 0, 1, 2, 4, 8

/latency=(?P<latency>\d+)s httpcode=(?P<httpcode>\d+)/ {
    webserver_latency_by_code [$httpcode] = $latency
}
```

Expected metric families:

- `webserver_latency_by_code_bucket{le="..."}`
- `webserver_latency_by_code_sum`
- `webserver_latency_by_code_count`

## 3) Multiple Mtail Programs

You can register multiple contexts and bind them per entrypoint.

```
mtail {
    name nginx_access;
    script /etc/alligator/mtail/nginx_access.mtail;
}

mtail {
    name app_errors;
    script /etc/alligator/mtail/app_errors.mtail;
}

entrypoint {
    bind 0.0.0.0:19101;
    handler mtail;
    mtail nginx_access;
}

entrypoint {
    bind 0.0.0.0:19102;
    handler mtail;
    mtail app_errors;
}
```

## 4) Dynamic API Push

Register mtail context at runtime:

```
POST /api/v1/config
Content-Type: application/json

{
  "mtail": [
    {
      "name": "linecount",
      "script": "/etc/alligator/mtail/linecount.mtail"
    }
  ]
}
```

Delete context:

```
DELETE /api/v1/config
Content-Type: application/json

{
  "mtail": [
    { "name": "linecount" }
  ]
}
```

## 5) Debugging Tips

- Set mtail stage logs (`log_level_vm`, `log_level_parser`, etc.) in `mtail {}`.
- Set `log_level debug` on the `entrypoint {}` to inspect runtime behavior.
- If metrics are missing, verify:
  - script path exists and is readable by Alligator
  - `entrypoint.handler` is `mtail`
  - `entrypoint.mtail` references an existing mtail context name

## 6) Input Shape Expectations

The mtail handler processes text streams line by line (`\n` separated):

- complete lines are executed immediately
- partial trailing line is buffered
- buffered tail is prepended to the next incoming chunk

This behavior is important for TCP/HTTP chunked log delivery where records can split across packets.

## 7) Upstream Google mtail Script Examples

For ready-to-use mtail programs, see the official examples directory:

- https://github.com/google/mtail/tree/main/examples

Useful starter scripts:

- postfix: https://github.com/google/mtail/blob/main/examples/postfix.mtail
- linecount: https://github.com/google/mtail/blob/main/examples/linecount.mtail
- histogram: https://github.com/google/mtail/blob/main/examples/histogram.mtail
- mysql slow queries: https://github.com/google/mtail/blob/main/examples/mysql_slowqueries.mtail
- apache combined: https://github.com/google/mtail/blob/main/examples/apache_combined.mtail

Recommended workflow:

1. Copy one script into `/etc/alligator/mtail/`.
2. Adapt regexes and metric names to your log format.
3. Register it in Alligator via `mtail { name ...; script ...; }`.
